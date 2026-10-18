// Datagram sockets: UDP, and ICMP echo ("ping sockets", as on Linux:
// the process sends echo requests, the kernel sets their id and
// checksum, and the replies with that id come back to the socket).
//
// A socket is a file (FD_SOCK). Received datagrams wait in a queue of
// up to QMAX, each in a page; more are dropped, as networks do.

#include "types.h"
#include "param.h"
#include "memlayout.h"
#include "defs.h"
#include "console.h"
#include "spinlock.h"
#include "sleeplock.h"
#include "proc.h"
#include "fs.h"
#include "file.h"
#include "timer.h"
#include "socket.h"
#include "net.h"

#define NSOCK 32
#define QMAX  16
#define EPHEMERAL 49152   // ports from here on are picked for sockets

struct pkt {
    struct pkt *next;
    uint32 src;      // sender's address and port
    uint16 sport;
    int len;
    char data[];
};

struct sock {
    struct spinlock lock;
    int proto;            // IPPROTO_UDP or IPPROTO_ICMP; 0: free
    uint16 port;          // UDP: local port; ICMP: echo id; 0: none yet
    uint32 peer;          // connect(): where send()/write() go
    uint16 peerport;
    struct pkt *head, *tail;
    int qlen;
    uint64 timeout;       // receive timeout in ticks; 0: none
    int waiting;          // a receiver sleeps (with a timeout)
};

static struct sock socks[NSOCK];
static struct spinlock socklock = { .name = "socks" };   // proto and port
static uint16 nextport = EPHEMERAL;

void sockinit(void)
{
    for (int i = 0; i < NSOCK; i++)
        initlock(&socks[i].lock, "sock");
}

// Is port free for proto? socklock held.
static int port_free(int proto, uint16 port)
{
    for (int i = 0; i < NSOCK; i++)
        if (socks[i].proto == proto && socks[i].port == port)
            return 0;
    return 1;
}

// Give s a port (an ICMP echo id) if it has none. Returns 0, or -1.
static int sock_autobind(struct sock *s)
{
    acquire(&socklock);
    for (int tries = 0; s->port == 0 && tries < 65536 - EPHEMERAL; tries++) {
        uint16 p = nextport;
        nextport = nextport == 65535 ? EPHEMERAL : nextport + 1;
        if (port_free(s->proto, p))
            s->port = p;
    }
    release(&socklock);
    return s->port ? 0 : -1;
}

void sock_deliver(int proto, uint16 port, uint32 src, uint16 sport, const char *data, int len)
{
    struct sock *s = 0;

    acquire(&socklock);
    for (int i = 0; i < NSOCK; i++)
        if (socks[i].proto == proto && socks[i].port == port)
            s = &socks[i];
    if (s)
        acquire(&s->lock);
    release(&socklock);
    if (s == 0)
        return;   // nobody listens: dropped

    struct pkt *p;
    if (s->qlen < QMAX && len <= PGSIZE - sizeof(*p) && (p = kalloc()) != 0) {
        p->next = 0;
        p->src = src;
        p->sport = sport;
        p->len = len;
        memmove(p->data, data, len);
        if (s->tail)
            s->tail->next = p;
        else
            s->head = p;
        s->tail = p;
        s->qlen++;
        wakeup(s);
    }
    release(&s->lock);
}

// Every tick (clockintr()): wake receivers that wait with a timeout,
// to check it.
void sock_tick(void)
{
    for (int i = 0; i < NSOCK; i++)
        if (__atomic_load_n(&socks[i].waiting, __ATOMIC_RELAXED)) {
            acquire(&socks[i].lock);
            wakeup(&socks[i]);
            release(&socks[i].lock);
        }
}

void sock_close(struct sock *s)
{
    acquire(&socklock);
    acquire(&s->lock);
    struct pkt *p = s->head;
    s->head = s->tail = 0;
    s->qlen = 0;
    s->proto = 0;
    s->port = 0;
    s->peer = 0;
    s->peerport = 0;
    s->timeout = 0;
    release(&s->lock);
    release(&socklock);
    while (p) {
        struct pkt *next = p->next;
        kfree(p);
        p = next;
    }
}

// Receive a datagram into user address dst (up to n bytes), waiting if
// none has arrived unless dontwait. The sender goes to *src, *sport.
// Returns its length, or -1 (timeout, signal, nothing with dontwait).
static int sock_recv(struct sock *s, uint64 dst, int n, int dontwait, uint32 *src, uint16 *sport)
{
    acquire(&s->lock);
    uint64 t0 = ticks;
    while (s->head == 0) {
        if (dontwait || interrupted(myproc()) || (s->timeout && ticks - t0 >= s->timeout)) {
            release(&s->lock);
            return -1;
        }
        s->waiting += s->timeout != 0;
        sleep(s, &s->lock);
        s->waiting -= s->timeout != 0;
    }
    struct pkt *p = s->head;
    s->head = p->next;
    if (s->head == 0)
        s->tail = 0;
    s->qlen--;
    release(&s->lock);

    int len = p->len < n ? p->len : n;
    int r = copyout(myproc()->pagetable, dst, p->data, len) < 0 ? -1 : len;
    *src = p->src;
    *sport = p->sport;
    kfree(p);
    return r;
}

// Send n bytes at user address src to dst, port dport. Returns n, or -1.
static int sock_send(struct sock *s, uint64 src, int n, uint32 dst, uint16 dport)
{
    if (n < 0 || n > UDP_MAXDATA || sock_autobind(s) < 0)
        return -1;
    char *buf = kalloc();
    if (buf == 0)
        return -1;
    int r = -1;
    if (copyin(myproc()->pagetable, buf, src, n) == 0) {
        if (s->proto == IPPROTO_UDP) {
            r = net_send_udp(dst, s->port, dport, buf, n);
        } else if (n >= 8 && buf[0] == 8) {
            // an echo request: with our id
            buf[4] = s->port >> 8;
            buf[5] = s->port;
            r = net_send_icmp(dst, buf, n);
        }
    }
    kfree(buf);
    return r < 0 ? -1 : n;
}

int sockread(struct sock *s, uint64 dst, int n)
{
    uint32 src;
    uint16 sport;
    return sock_recv(s, dst, n, 0, &src, &sport);
}

int sockwrite(struct sock *s, uint64 src, int n)
{
    if (s->peer == 0)
        return -1;   // not connected
    return sock_send(s, src, n, s->peer, s->peerport);
}

// The socket of file descriptor n, or 0.
static struct sock *argsock(int n)
{
    int fd;
    struct file *f;
    argint(n, &fd);
    if (fd < 0 || fd >= NOFILE || (f = myproc()->ofile[fd]) == 0 || f->type != FD_SOCK)
        return 0;
    return f->sock;
}

// A struct sockaddr_in from user address addr (len bytes).
static int argaddr_in(uint64 addr, int len, struct sockaddr_in *sa)
{
    if (len < sizeof(*sa) || copyin(myproc()->pagetable, (char *)sa, addr, sizeof(*sa)) < 0 ||
        sa->sin_family != AF_INET)
        return -1;
    return 0;
}

// socket(domain, type, protocol)
uint64 sys_socket(void)
{
    int domain, type, protocol;
    argint(0, &domain);
    argint(1, &type);
    argint(2, &protocol);
    if (domain != AF_INET || type != SOCK_DGRAM)
        return -1;
    if (protocol == 0)
        protocol = IPPROTO_UDP;
    if (protocol != IPPROTO_UDP && protocol != IPPROTO_ICMP)
        return -1;
    if (!virtio_net_present())
        return -1;

    struct sock *s = 0;
    acquire(&socklock);
    for (int i = 0; i < NSOCK && s == 0; i++)
        if (socks[i].proto == 0)
            s = &socks[i];
    if (s)
        s->proto = protocol;
    release(&socklock);
    if (s == 0)
        return -1;

    struct file *f = filealloc();
    int fd = -1;
    if (f == 0 || (fd = fdalloc(f)) < 0) {
        if (f)
            fileclose(f);   // FD_NONE: just freed
        sock_close(s);
        return -1;
    }
    f->type = FD_SOCK;
    f->sock = s;
    f->readable = f->writable = 1;
    return fd;
}

// bind(fd, addr, len): use the port in addr (UDP) or echo id (ICMP).
uint64 sys_bind(void)
{
    struct sock *s = argsock(0);
    uint64 addr;
    int len;
    struct sockaddr_in sa;
    argaddr(1, &addr);
    argint(2, &len);
    if (s == 0 || argaddr_in(addr, len, &sa) < 0 || s->port != 0)
        return -1;
    uint16 port = ntohs(sa.sin_port);
    if (port == 0)
        return sock_autobind(s);
    acquire(&socklock);
    int ok = port_free(s->proto, port);
    if (ok)
        s->port = port;
    release(&socklock);
    return ok ? 0 : -1;
}

// connect(fd, addr, len): where send()/write() go.
uint64 sys_connect(void)
{
    struct sock *s = argsock(0);
    uint64 addr;
    int len;
    struct sockaddr_in sa;
    argaddr(1, &addr);
    argint(2, &len);
    if (s == 0 || argaddr_in(addr, len, &sa) < 0 || sock_autobind(s) < 0)
        return -1;
    s->peer = ntohl(sa.sin_addr.s_addr);
    s->peerport = ntohs(sa.sin_port);
    return 0;
}

// sendto(fd, buf, n, flags, addr, len)
uint64 sys_sendto(void)
{
    struct sock *s = argsock(0);
    uint64 buf, addr;
    int n, flags, len;
    struct sockaddr_in sa;
    argaddr(1, &buf);
    argint(2, &n);
    argint(3, &flags);
    argaddr(4, &addr);
    argint(5, &len);
    if (s == 0)
        return -1;
    if (addr == 0)
        return s->peer ? sock_send(s, buf, n, s->peer, s->peerport) : -1;
    if (argaddr_in(addr, len, &sa) < 0)
        return -1;
    return sock_send(s, buf, n, ntohl(sa.sin_addr.s_addr), ntohs(sa.sin_port));
}

// recvfrom(fd, buf, n, flags, addr, lenp): the sender's address goes
// to addr unless it is 0.
uint64 sys_recvfrom(void)
{
    struct sock *s = argsock(0);
    uint64 buf, addr, lenp;
    int n, flags;
    argaddr(1, &buf);
    argint(2, &n);
    argint(3, &flags);
    argaddr(4, &addr);
    argaddr(5, &lenp);
    if (s == 0 || n < 0)
        return -1;
    uint32 src;
    uint16 sport;
    int r = sock_recv(s, buf, n, flags & MSG_DONTWAIT, &src, &sport);
    if (r >= 0 && addr) {
        struct sockaddr_in sa;
        memset(&sa, 0, sizeof(sa));
        sa.sin_family = AF_INET;
        sa.sin_port = htons(sport);
        sa.sin_addr.s_addr = htonl(src);
        int len = sizeof(sa);
        if (copyout(myproc()->pagetable, addr, (char *)&sa, sizeof(sa)) < 0 ||
            (lenp && copyout(myproc()->pagetable, lenp, (char *)&len, sizeof(len)) < 0))
            return -1;
    }
    return r;
}

// setsockopt(fd, level, name, val, len): only SO_RCVTIMEO.
uint64 sys_setsockopt(void)
{
    struct sock *s = argsock(0);
    int level, name, len;
    uint64 val;
    struct timeval tv;
    argint(1, &level);
    argint(2, &name);
    argaddr(3, &val);
    argint(4, &len);
    if (s == 0 || level != SOL_SOCKET || name != SO_RCVTIMEO || len < sizeof(tv) ||
        copyin(myproc()->pagetable, (char *)&tv, val, sizeof(tv)) < 0)
        return -1;
    uint64 t = tv.tv_sec * HZ + (tv.tv_usec * HZ + 999999) / 1000000;
    acquire(&s->lock);
    s->timeout = t;
    release(&s->lock);
    return 0;
}
