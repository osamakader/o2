// DHCP: the address, netmask, gateway and DNS server from the network's
// server. A kernel thread ("dhcp") broadcasts a DISCOVER, takes the
// first OFFER and REQUESTs it; the ACK sets net_ip and the rest. It
// asks again when half the lease is over. On qemu's user-mode network
// the server gives what net.c starts with anyway.

#include "types.h"
#include "param.h"
#include "memlayout.h"
#include "defs.h"
#include "console.h"
#include "spinlock.h"
#include "proc.h"
#include "timer.h"
#include "socket.h"
#include "net.h"

#define DHCP_SERVER_PORT 67
#define DHCP_CLIENT_PORT 68
#define MAGIC 0x63825363

#define DISCOVER 1
#define OFFER    2
#define REQUEST  3
#define ACK      5
#define NAK      6

struct dhcp {
    uchar op, htype, hlen, hops;
    uint32 xid;
    uint16 secs, flags;
    uint32 ciaddr, yiaddr, siaddr, giaddr;
    uchar chaddr[16];
    uchar sname[64];
    uchar file[128];
    uint32 magic;
    uchar options[312];
} __attribute__((packed));

// the answer the thread waits for
static struct {
    struct spinlock lock;
    int ready;
    struct dhcp msg;
    int len;
} reply = { .lock = { .name = "dhcp" } };

static uint32 xid;

// A DHCP message for us (the network interrupt, UDP port 68).
void dhcp_rx(const char *data, int len)
{
    const struct dhcp *d = (const struct dhcp *)data;
    if (len < 240 || len > sizeof(struct dhcp) || d->op != 2 || ntohl(d->xid) != xid ||
        ntohl(d->magic) != MAGIC)
        return;
    acquire(&reply.lock);
    if (!reply.ready) {
        memset(&reply.msg, 0, sizeof(reply.msg));
        memmove(&reply.msg, data, len);
        reply.len = len;
        reply.ready = 1;
    }
    release(&reply.lock);
}

// Option code's value in d: its length in *n, or 0 if absent.
static const uchar *option(const struct dhcp *d, int len, int code, int *n)
{
    const uchar *p = d->options, *end = (const uchar *)d + len;
    while (p < end && *p != 255) {
        if (*p == 0) {
            p++;
            continue;
        }
        if (p + 2 > end || p + 2 + p[1] > end)
            break;
        if (*p == code) {
            *n = p[1];
            return p + 2;
        }
        p += 2 + p[1];
    }
    return 0;
}

static uint32 be32(const uchar *p)
{
    return (uint32)p[0] << 24 | p[1] << 16 | p[2] << 8 | p[3];
}

// Send a DISCOVER, or a REQUEST for ip from server.
static void send_msg(int type, uint32 ip, uint32 server)
{
    struct dhcp *d = kalloc();
    if (d == 0)
        return;
    memset(d, 0, sizeof(*d));
    d->op = 1;
    d->htype = 1;
    d->hlen = 6;
    d->xid = htonl(xid);
    d->flags = htons(0x8000);   // answer by broadcast: we have no address
    memmove(d->chaddr, netif.mac, 6);
    d->magic = htonl(MAGIC);
    uchar *o = d->options;
    *o++ = 53;   // message type
    *o++ = 1;
    *o++ = type;
    if (type == REQUEST) {
        *o++ = 50;   // the address wanted
        *o++ = 4;
        for (int i = 3; i >= 0; i--)
            *o++ = ip >> (8 * i);
        if (server) {
            *o++ = 54;   // from this server
            *o++ = 4;
            for (int i = 3; i >= 0; i--)
                *o++ = server >> (8 * i);
        }
    }
    *o++ = 55;   // asking for: netmask, router, DNS server
    *o++ = 3;
    *o++ = 1;
    *o++ = 3;
    *o++ = 6;
    *o++ = 255;
    int len = o - (uchar *)d;
    if (len < 300)
        len = 300;   // BOOTP's minimum
    net_send_udp(0xffffffff, DHCP_CLIENT_PORT, DHCP_SERVER_PORT, (char *)d, len);
    kfree(d);
}

// Wait up to secs seconds for an answer of type want (or a NAK); it
// goes in *out. Returns its type, or 0 on timeout.
static int await(int want, int secs, struct dhcp *out, int *len)
{
    for (int t = 0; t < secs * HZ; t++) {
        acquire(&reply.lock);
        if (reply.ready) {
            int n;
            const uchar *type = option(&reply.msg, reply.len, 53, &n);
            reply.ready = 0;
            if (type && n == 1 && (*type == want || *type == NAK)) {
                *out = reply.msg;
                *len = reply.len;
                release(&reply.lock);
                return *type;
            }
        }
        release(&reply.lock);
        sleep_ticks(1);
    }
    return 0;
}

static void print_addr(const char *label, uint32 a)
{
    printf("%s%d.%d.%d.%d", label, a >> 24, (a >> 16) & 0xff, (a >> 8) & 0xff, a & 0xff);
}

// DISCOVER, OFFER, REQUEST, ACK. Returns the lease in seconds, or 0.
static uint32 lease(void)
{
    static struct dhcp m;
    int len, n;
    xid = (xid * 1103515245 + 12345) ^ (uint32)timer_ns();
    acquire(&reply.lock);
    reply.ready = 0;
    release(&reply.lock);

    send_msg(DISCOVER, 0, 0);
    if (await(OFFER, 3, &m, &len) != OFFER)
        return 0;
    uint32 ip = ntohl(m.yiaddr), server = 0;
    const uchar *p = option(&m, len, 54, &n);
    if (p && n == 4)
        server = be32(p);

    send_msg(REQUEST, ip, server);
    if (await(ACK, 3, &m, &len) != ACK)
        return 0;
    uint32 mask = 0, gw = 0, dns = 0, secs = 3600;
    if ((p = option(&m, len, 1, &n)) && n == 4)
        mask = be32(p);
    if ((p = option(&m, len, 3, &n)) && n >= 4)
        gw = be32(p);
    if ((p = option(&m, len, 6, &n)) && n >= 4)
        dns = be32(p);
    if ((p = option(&m, len, 51, &n)) && n == 4)
        secs = be32(p);
    if (mask == 0)
        mask = IPADDR(255, 255, 255, 0);
    int changed = ip != net_ip || mask != net_mask || gw != net_gateway || dns != net_dns;
    net_mask = mask;
    net_gateway = gw;
    net_dns = dns;
    net_ip = ip;
    if (changed) {
        print_addr("dhcp: ", ip);
        print_addr(", gateway ", gw);
        print_addr(", dns ", dns);
        printf(", lease %d s\n", secs);
    }
    return secs;
}

static void dhcp_thread(void *arg)
{
    for (;;) {
        uint32 secs = 0;
        for (int try = 0; try < 5 && secs == 0; try++)
            secs = lease();
        if (secs == 0) {
            if (net_ip == 0)
                printf("dhcp: no answer; trying again in a minute\n");
            sleep_ticks(60 * HZ);
            continue;
        }
        // again when half of it is over (at most a day: ticks fit)
        if (secs > 2 * 86400)
            secs = 2 * 86400;
        sleep_ticks((uint64)secs / 2 * HZ);
    }
}

// Start asking, once the interface is up.
void dhcp_start(void)
{
    struct proc *p = kthread_create(dhcp_thread, 0);
    if (p)
        safestrcpy(p->name, "dhcp", sizeof(p->name));
}
