// The network stack: Ethernet, ARP, IPv4, ICMP and UDP, enough for
// datagram sockets (socket.c) on qemu's user-mode network. Received
// frames come from the network driver's interrupt (net_rx()); sending
// happens in system calls, and for replies (ARP, ICMP echo) in the
// interrupt. No IP options, fragments or routing beyond a gateway.

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

#define ETH_TYPE_IP  0x0800
#define ETH_TYPE_ARP 0x0806

struct eth {
    uchar dst[6];
    uchar src[6];
    uint16 type;
} __attribute__((packed));

struct arp {
    uint16 htype;    // 1: Ethernet
    uint16 ptype;    // ETH_TYPE_IP
    uchar hlen, plen;
    uint16 op;       // 1 request, 2 reply
    uchar sha[6];
    uint32 sip;
    uchar tha[6];
    uint32 tip;
} __attribute__((packed));

struct ip {
    uchar vhl;       // version 4, header length 5 (words)
    uchar tos;
    uint16 len;
    uint16 id;
    uint16 off;
    uchar ttl;
    uchar proto;
    uint16 sum;
    uint32 src;
    uint32 dst;
} __attribute__((packed));

struct udp {
    uint16 sport, dport, len, sum;
} __attribute__((packed));

struct icmp {
    uchar type, code;
    uint16 sum;
    uint16 id, seq;
} __attribute__((packed));

#define ICMP_ECHOREPLY 0
#define ICMP_ECHO      8

static const uchar broadcast[6] = { 0xff, 0xff, 0xff, 0xff, 0xff, 0xff };

// The Internet checksum of n bytes at p, plus sum (host order).
static uint16 cksum(const void *p, int n, uint32 sum)
{
    const uchar *b = p;
    for (int i = 0; i + 1 < n; i += 2)
        sum += (b[i] << 8) | b[i + 1];
    if (n & 1)
        sum += b[n - 1] << 8;
    while (sum >> 16)
        sum = (sum & 0xffff) + (sum >> 16);
    return ~sum;
}

// ARP cache: IP (host order) to MAC, for the hosts on our network.
#define NARP 16
static struct {
    struct spinlock lock;
    struct {
        uint32 ip;
        uchar mac[6];
    } e[NARP];
    int next;   // replace round-robin
} arp = { .lock = { .name = "arp" } };

static int arp_lookup(uint32 ip, uchar *mac)
{
    int found = 0;
    acquire(&arp.lock);
    for (int i = 0; i < NARP; i++) {
        if (arp.e[i].ip == ip && ip != 0) {
            memmove(mac, arp.e[i].mac, 6);
            found = 1;
            break;
        }
    }
    release(&arp.lock);
    return found;
}

static void arp_update(uint32 ip, const uchar *mac)
{
    acquire(&arp.lock);
    int i;
    for (i = 0; i < NARP && arp.e[i].ip != ip; i++)
        ;
    if (i == NARP) {
        i = arp.next;
        arp.next = (arp.next + 1) % NARP;
    }
    arp.e[i].ip = ip;
    memmove(arp.e[i].mac, mac, 6);
    release(&arp.lock);
}

static void arp_send(int op, const uchar *dstmac, const uchar *tha, uint32 tip)
{
    struct {
        struct eth eth;
        struct arp arp;
    } __attribute__((packed)) f;

    memmove(f.eth.dst, dstmac, 6);
    memmove(f.eth.src, net_mac, 6);
    f.eth.type = htons(ETH_TYPE_ARP);
    f.arp.htype = htons(1);
    f.arp.ptype = htons(ETH_TYPE_IP);
    f.arp.hlen = 6;
    f.arp.plen = 4;
    f.arp.op = htons(op);
    memmove(f.arp.sha, net_mac, 6);
    f.arp.sip = htonl(NET_IP);
    memmove(f.arp.tha, tha, 6);
    f.arp.tip = htonl(tip);
    virtio_net_send(&f, sizeof(f));
}

static int on_net(uint32 ip)
{
    return (ip & NET_MASK) == (NET_IP & NET_MASK);
}

// The MAC to send a packet for dst to: dst's own on our network, the
// gateway's otherwise. If it isn't known, ask; with wait (process
// context), for up to a second. Returns 0, or -1.
static int resolve(uint32 dst, uchar *mac, int wait)
{
    if (dst == 0xffffffff) {
        memmove(mac, broadcast, 6);
        return 0;
    }
    uint32 hop = on_net(dst) ? dst : NET_GATEWAY;
    for (int t = 0; ; t++) {
        if (arp_lookup(hop, mac))
            return 0;
        if (t % (HZ / 3) == 0 && t < HZ)
            arp_send(1, broadcast, (uchar[6]){ 0 }, hop);
        if (!wait || t >= HZ || interrupted(myproc()))
            return -1;
        sleep_ticks(1);
    }
}

static uint16 ip_id;

// Send an IP packet with len bytes of payload for protocol proto to
// dst. Returns 0, or -1.
static int ip_send(uint32 dst, int proto, const char *payload, int len, int wait)
{
    uchar mac[6];
    if (len > UDP_MAXDATA + sizeof(struct udp) || resolve(dst, mac, wait) < 0)
        return -1;
    char *f = kalloc();
    if (f == 0)
        return -1;
    struct eth *eth = (struct eth *)f;
    struct ip *ip = (struct ip *)(eth + 1);
    memmove(eth->dst, mac, 6);
    memmove(eth->src, net_mac, 6);
    eth->type = htons(ETH_TYPE_IP);
    ip->vhl = 0x45;
    ip->tos = 0;
    ip->len = htons(sizeof(*ip) + len);
    ip->id = htons(__atomic_fetch_add(&ip_id, 1, __ATOMIC_RELAXED));
    ip->off = 0;
    ip->ttl = 64;
    ip->proto = proto;
    ip->sum = 0;
    ip->src = htonl(NET_IP);
    ip->dst = htonl(dst);
    ip->sum = htons(cksum(ip, sizeof(*ip), 0));
    memmove(ip + 1, payload, len);
    int r = virtio_net_send(f, sizeof(*eth) + sizeof(*ip) + len);
    kfree(f);
    return r;
}

// Send len bytes of data in a UDP datagram from port sport to dst's
// port dport. Process context. Returns 0, or -1.
int net_send_udp(uint32 dst, uint16 sport, uint16 dport, const char *data, int len)
{
    if (len > UDP_MAXDATA)
        return -1;
    char *p = kalloc();
    if (p == 0)
        return -1;
    struct udp *u = (struct udp *)p;
    u->sport = htons(sport);
    u->dport = htons(dport);
    u->len = htons(sizeof(*u) + len);
    u->sum = 0;
    memmove(u + 1, data, len);
    // checksum over a pseudo-header (addresses, protocol, length), too
    uint32 pseudo = (NET_IP >> 16) + (NET_IP & 0xffff) + (dst >> 16) + (dst & 0xffff) +
                    IPPROTO_UDP + sizeof(*u) + len;
    uint16 sum = cksum(u, sizeof(*u) + len, pseudo);
    u->sum = htons(sum ? sum : 0xffff);
    int r = ip_send(dst, IPPROTO_UDP, p, sizeof(*u) + len, 1);
    kfree(p);
    return r;
}

// Send an ICMP message (msg, len bytes, starting with its header) to
// dst, filling in its checksum. Process context. Returns 0, or -1.
int net_send_icmp(uint32 dst, char *msg, int len)
{
    struct icmp *ic = (struct icmp *)msg;
    if (len < sizeof(*ic))
        return -1;
    ic->sum = 0;
    ic->sum = htons(cksum(msg, len, 0));
    return ip_send(dst, IPPROTO_ICMP, msg, len, 1);
}

static void arp_rx(struct arp *a, int len)
{
    if (len < sizeof(*a) || ntohs(a->htype) != 1 || ntohs(a->ptype) != ETH_TYPE_IP)
        return;
    uint32 sip = ntohl(a->sip);
    if (sip != 0 && on_net(sip))
        arp_update(sip, a->sha);
    if (ntohs(a->op) == 1 && ntohl(a->tip) == NET_IP)
        arp_send(2, a->sha, a->sha, sip);   // that's us
}

static void icmp_rx(uint32 src, char *msg, int len)
{
    struct icmp *ic = (struct icmp *)msg;
    if (len < sizeof(*ic) || cksum(msg, len, 0) != 0)
        return;
    if (ic->type == ICMP_ECHO) {
        // answer a ping, from the interrupt: the sender is in the ARP
        // cache (ip_rx()) if it is on our network
        ic->type = ICMP_ECHOREPLY;
        ic->sum = 0;
        ic->sum = htons(cksum(msg, len, 0));
        ip_send(src, IPPROTO_ICMP, msg, len, 0);
    } else if (ic->type == ICMP_ECHOREPLY) {
        sock_deliver(IPPROTO_ICMP, ntohs(ic->id), src, 0, msg, len);
    }
}

static void ip_rx(struct eth *eth, struct ip *ip, int len)
{
    if (len < sizeof(*ip) || ip->vhl != 0x45 || cksum(ip, sizeof(*ip), 0) != 0)
        return;   // not IPv4, has options, or damaged
    int tot = ntohs(ip->len);
    uint32 src = ntohl(ip->src), dst = ntohl(ip->dst);
    if (tot > len || tot < sizeof(*ip) || (dst != NET_IP && dst != 0xffffffff))
        return;
    if (ntohs(ip->off) & 0x3fff)
        return;   // a fragment
    if (on_net(src))
        arp_update(src, eth->src);
    char *payload = (char *)(ip + 1);
    int n = tot - sizeof(*ip);
    if (ip->proto == IPPROTO_ICMP) {
        icmp_rx(src, payload, n);
    } else if (ip->proto == IPPROTO_UDP && n >= sizeof(struct udp)) {
        struct udp *u = (struct udp *)payload;
        int ulen = ntohs(u->len);
        if (ulen < sizeof(*u) || ulen > n)
            return;
        sock_deliver(IPPROTO_UDP, ntohs(u->dport), src, ntohs(u->sport),
                     (char *)(u + 1), ulen - sizeof(*u));
    }
}

// A frame arrived (from the driver's interrupt).
void net_rx(char *frame, int len)
{
    struct eth *eth = (struct eth *)frame;
    if (len < sizeof(*eth))
        return;
    if (ntohs(eth->type) == ETH_TYPE_ARP)
        arp_rx((struct arp *)(eth + 1), len - sizeof(*eth));
    else if (ntohs(eth->type) == ETH_TYPE_IP)
        ip_rx(eth, (struct ip *)(eth + 1), len - sizeof(*eth));
}
