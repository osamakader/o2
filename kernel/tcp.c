// TCP: stream sockets that connect() to a server. A connection (a TCB)
// keeps what was received until read() takes it, and what was written
// until the peer acknowledges it, each in a 16KB ring; the free room
// in the first is the window we advertise.
//
// Segments arrive from the network interrupt (tcp_rx()); system calls
// and the timer (tcp_tick(), every tick) send, under the TCB's lock,
// so never waiting for ARP (connect() does that first).
//
// Lost segments: three duplicate ACKs send the first unacknowledged
// one again at once (fast retransmit); the timer, from the round trip
// time measured (RFC 6298, Karn), everything from it on (go-back-N),
// each time after twice as long, up to TCP_TRIES times. Segments that
// arrive out of order are kept in the ring, in up to NOOO pieces,
// until what is missing before them comes. No window scaling, no
// delayed ACKs, no Nagle. A closed socket's connection lives on until
// both FINs are acknowledged, then 2 seconds of TIME_WAIT.

#include "types.h"
#include "param.h"
#include "memlayout.h"
#include "defs.h"
#include "console.h"
#include "spinlock.h"
#include "proc.h"
#include "timer.h"
#include "socket.h"
#include "poll.h"
#include "net.h"

#define NTCB      16
#define BUFPAGES  4
#define BUFSIZE   (BUFPAGES * PGSIZE)   // each ring
#define TCP_MSS   1460                  // a 1500-byte packet's worth
#define TCP_TRIES 8
#define RTO_MIN   (HZ / 5)
#define RTO_MAX   (16 * HZ)
#define NOOO      8                     // pieces received out of order
#define TIMEWAIT  (2 * HZ)

#define FIN 0x01
#define SYN 0x02
#define RST 0x04
#define PSH 0x08
#define ACK 0x10

struct tcphdr {
    uint16 sport, dport;
    uint32 seq, ack;
    uchar off;        // header length in words, in the top 4 bits
    uchar flags;
    uint16 win, sum, urg;
} __attribute__((packed));

enum { CLOSED, SYN_SENT, ESTABLISHED, FIN_WAIT_1, FIN_WAIT_2, CLOSE_WAIT, CLOSING,
       LAST_ACK, TIME_WAIT };

// sequence numbers wrap: compare them as differences
#define SEQ_LT(a, b)  ((int)((a) - (b)) < 0)
#define SEQ_LEQ(a, b) ((int)((a) - (b)) <= 0)

// a byte ring of BUFPAGES pages
struct ring {
    char *pg[BUFPAGES];
    int head, len;      // the first byte, and how many
};

struct tcb {
    struct spinlock lock;
    int used;
    int state;
    uint16 lport, rport;
    uint32 rip;
    uint32 iss, snd_una, snd_nxt, snd_wnd;   // snd_una is snd.head's byte
    uint32 snd_max;     // the highest sent (snd_nxt goes back to resend)
    uint32 rcv_nxt;
    struct { uint32 start, end; } ooo[NOOO];  // out of order, in rcv past len
    int nooo;
    int dupacks;
    int timing;         // a segment's round trip is being timed: up to rtseq
    uint32 rtseq;
    uint64 rtstart;
    int srtt8, rttvar4; // smoothed round trip time x8, its variation x4 (ticks)
    int mss;            // the peer's
    int adv;            // the window we advertised last
    struct ring snd, rcv;
    int closing;        // close(): FIN after the data
    int finsent;        // FIN sent; it is sequence number finseq
    uint32 finseq;
    int finrcvd;        // read() returns 0 when rcv is empty
    int err;            // reset, refused, timed out
    int fileopen;       // a socket still has it
    uint64 deadline;    // retransmission (or TIME_WAIT end), in ticks; 0: none
    int rto, tries;
    int probe;          // the timer may send into a zero window
};

static struct tcb tcbs[NTCB];

void tcp_init(void)
{
    for (int i = 0; i < NTCB; i++)
        initlock(&tcbs[i].lock, "tcb");
}

static char *ringat(struct ring *r, int i)
{
    i = (r->head + i) % BUFSIZE;
    return r->pg[i / PGSIZE] + i % PGSIZE;
}

// n bytes from i on in r to dst (a kernel address)
static void ring_copyout(struct ring *r, int i, char *dst, int n)
{
    for (int k = 0; k < n; k++)
        dst[k] = *ringat(r, i + k);
}

static void ring_drop(struct ring *r, int n)
{
    r->head = (r->head + n) % BUFSIZE;
    r->len -= n;
}

static void ring_free(struct ring *r)
{
    for (int i = 0; i < BUFPAGES; i++) {
        if (r->pg[i])
            kfree(r->pg[i]);
        r->pg[i] = 0;
    }
}

static int ring_alloc(struct ring *r)
{
    r->head = r->len = 0;
    for (int i = 0; i < BUFPAGES; i++)
        if ((r->pg[i] = kalloc()) == 0)
            return -1;
    return 0;
}

// Let t go once nothing uses it. t->lock held.
static void tcb_maybe_free(struct tcb *t)
{
    if (t->state != CLOSED || t->fileopen)
        return;
    ring_free(&t->snd);
    ring_free(&t->rcv);
    t->used = 0;
}

// Send a segment: flags, len bytes of t->snd from offset off (seq is
// that byte's sequence number), acknowledging what we have received.
// t->lock held (or t not yet shared).
static void send_seg(struct tcb *t, uint32 seq, int flags, int off, int len)
{
    char *p = kalloc();
    if (p == 0)
        return;
    struct tcphdr *h = (struct tcphdr *)p;
    int hlen = sizeof(*h);
    if (flags & SYN) {
        // our MSS option
        uchar *o = (uchar *)(h + 1);
        o[0] = 2;
        o[1] = 4;
        o[2] = TCP_MSS >> 8;
        o[3] = TCP_MSS & 0xff;
        hlen += 4;
    }
    h->sport = htons(t->lport);
    h->dport = htons(t->rport);
    h->seq = htonl(seq);
    h->ack = htonl(flags & ACK ? t->rcv_nxt : 0);
    h->off = (hlen / 4) << 4;
    h->flags = flags;
    t->adv = BUFSIZE - t->rcv.len;
    h->win = htons(t->adv > 0xffff ? 0xffff : t->adv);
    h->sum = 0;
    h->urg = 0;
    ring_copyout(&t->snd, off, p + hlen, len);
    net_send_tcp(t->rip, p, hlen + len);
    kfree(p);
}

static void arm(struct tcb *t)
{
    if (t->deadline == 0)
        t->deadline = ticks + t->rto;
}

// A round trip took r ticks: the timeout from it (RFC 6298).
static void rtt_sample(struct tcb *t, int r)
{
    if (t->srtt8 == 0) {
        t->srtt8 = r * 8 + 1;
        t->rttvar4 = r * 2;
    } else {
        int err = r - t->srtt8 / 8;
        t->srtt8 += err;
        t->rttvar4 += (err < 0 ? -err : err) - t->rttvar4 / 4;
    }
}

static int rto(struct tcb *t)
{
    if (t->srtt8 == 0)
        return HZ;   // nothing measured yet: 1s
    int r = t->srtt8 / 8 + t->rttvar4;
    return r < RTO_MIN ? RTO_MIN : r > RTO_MAX ? RTO_MAX : r;
}

// Send what may be sent: new data up to the peer's window, then our
// FIN once the data is out. t->lock held.
static void output(struct tcb *t)
{
    if (t->state != ESTABLISHED && t->state != CLOSE_WAIT && t->state != FIN_WAIT_1 &&
        t->state != CLOSING && t->state != LAST_ACK)
        return;
    int wnd = t->snd_wnd ? t->snd_wnd : t->probe;
    for (;;) {
        int sent = t->snd_nxt - t->snd_una - t->finsent;   // data in flight
        int unsent = t->snd.len - sent;
        int room = wnd - (int)(t->snd_nxt - t->snd_una);
        if (unsent <= 0 || room <= 0)
            break;
        int n = unsent < t->mss ? unsent : t->mss;
        if (n > room)
            n = room;
        send_seg(t, t->snd_nxt, ACK | (n == unsent ? PSH : 0), sent, n);
        if (!t->timing && t->snd_nxt == t->snd_max) {
            // new data (not sent before): time it
            t->timing = 1;
            t->rtseq = t->snd_nxt + n;
            t->rtstart = ticks;
        }
        t->snd_nxt += n;
        if (SEQ_LT(t->snd_max, t->snd_nxt))
            t->snd_max = t->snd_nxt;
        arm(t);
    }
    if (t->snd_wnd == 0 && t->snd_nxt == t->snd_una && t->snd.len > 0)
        arm(t);   // a zero window: the timer probes it
    if (t->closing && !t->finsent && t->snd_nxt - t->snd_una == t->snd.len) {
        send_seg(t, t->snd_nxt, FIN | ACK, 0, 0);
        t->finsent = 1;
        t->finseq = t->snd_nxt++;
        if (SEQ_LT(t->snd_max, t->snd_nxt))
            t->snd_max = t->snd_nxt;
        if (t->state == ESTABLISHED)
            t->state = FIN_WAIT_1;
        else if (t->state == CLOSE_WAIT)
            t->state = LAST_ACK;
        arm(t);
    }
}

static void wake(struct tcb *t)
{
    wakeup(t);
    pollwakeup();
}

static void enter_timewait(struct tcb *t)
{
    t->state = TIME_WAIT;
    t->deadline = ticks + TIMEWAIT;
}

static void reset(struct tcb *t, int err)
{
    t->state = CLOSED;
    t->err = err;
    t->deadline = 0;
    wake(t);
    tcb_maybe_free(t);
}

// A segment for no connection: answer with a reset (RFC 793).
static void send_reset(uint32 src, struct tcphdr *h, int len)
{
    if (h->flags & RST)
        return;
    struct tcb t;
    memset(&t, 0, sizeof(t));
    t.lport = ntohs(h->dport);
    t.rport = ntohs(h->sport);
    t.rip = src;
    t.rcv_nxt = ntohl(h->seq) + len + !!(h->flags & SYN) + !!(h->flags & FIN);
    t.adv = 0;
    // no rings: send_seg() copies 0 bytes of them
    if (h->flags & ACK)
        send_seg(&t, ntohl(h->ack), RST, 0, 0);
    else
        send_seg(&t, 0, RST | ACK, 0, 0);
}

// The peer's MSS option, if the SYN has one.
static int peer_mss(struct tcphdr *h, int hlen)
{
    uchar *o = (uchar *)(h + 1), *end = (uchar *)h + hlen;
    while (o < end && *o != 0) {
        if (*o == 1) {
            o++;
            continue;
        }
        if (o + 1 >= end || o[1] < 2)
            break;
        if (o[0] == 2 && o[1] == 4 && o + 4 <= end) {
            int m = o[2] << 8 | o[3];
            return m < TCP_MSS ? m : TCP_MSS;
        }
        o += o[1];
    }
    return 536;
}

// A TCP segment (len bytes, its checksum checked) from src.
void tcp_rx(uint32 src, char *seg, int len)
{
    struct tcphdr *h = (struct tcphdr *)seg;
    if (len < sizeof(*h))
        return;
    int hlen = (h->off >> 4) * 4;
    if (hlen < sizeof(*h) || hlen > len)
        return;
    uint16 dport = ntohs(h->dport), sport = ntohs(h->sport);
    char *data = seg + hlen;
    int dlen = len - hlen;
    uint32 seq = ntohl(h->seq), ack = ntohl(h->ack);
    int flags = h->flags;

    struct tcb *t = 0;
    for (int i = 0; i < NTCB && t == 0; i++) {
        struct tcb *c = &tcbs[i];
        acquire(&c->lock);
        if (c->used && c->state != CLOSED && c->lport == dport && c->rport == sport &&
            c->rip == src)
            t = c;
        else
            release(&c->lock);
    }
    if (t == 0) {
        send_reset(src, h, dlen);
        return;
    }

    if (t->state == SYN_SENT) {
        if ((flags & ACK) && ack != t->iss + 1) {
            send_reset(src, h, dlen);
        } else if (flags & RST) {
            if (flags & ACK)
                reset(t, -1);   // refused
        } else if ((flags & SYN) && (flags & ACK)) {
            t->rcv_nxt = seq + 1;
            t->snd_una = ack;
            t->snd_wnd = ntohs(h->win);
            t->mss = peer_mss(h, hlen);
            t->state = ESTABLISHED;
            t->deadline = 0;
            t->tries = 0;
            t->snd_max = t->snd_nxt;
            if (t->timing) {
                t->timing = 0;
                rtt_sample(t, ticks - t->rtstart);
            }
            t->rto = rto(t);
            send_seg(t, t->snd_nxt, ACK, 0, 0);
            wake(t);
            output(t);
        }
        release(&t->lock);
        return;
    }

    if (flags & RST) {
        // only one in the window counts (blind resets)
        if (seq == t->rcv_nxt || (SEQ_LEQ(t->rcv_nxt, seq) &&
                                  SEQ_LT(seq, t->rcv_nxt + BUFSIZE - t->rcv.len)))
            reset(t, -1);
        release(&t->lock);
        return;
    }
    if (flags & SYN) {
        // the SYN-ACK again: our ACK got lost
        send_seg(t, t->snd_nxt, ACK, 0, 0);
        release(&t->lock);
        return;
    }

    // what it acknowledges
    if ((flags & ACK) && SEQ_LT(t->snd_una, ack) && SEQ_LEQ(ack, t->snd_max)) {
        int n = ack - t->snd_una;
        int data = n < t->snd.len ? n : t->snd.len;
        ring_drop(&t->snd, data);
        t->snd_una = ack;
        if (SEQ_LT(t->snd_nxt, ack))
            t->snd_nxt = ack;   // past what a go-back sent again
        if (t->timing && SEQ_LEQ(t->rtseq, ack)) {
            t->timing = 0;
            rtt_sample(t, ticks - t->rtstart);
        }
        t->dupacks = 0;
        t->tries = 0;
        t->rto = rto(t);
        t->deadline = 0;
        if (t->snd_una != t->snd_nxt)
            arm(t);
        if (t->finsent && ack == t->finseq + 1) {
            // our FIN, too
            if (t->state == FIN_WAIT_1)
                t->state = FIN_WAIT_2;
            else if (t->state == CLOSING)
                enter_timewait(t);
            else if (t->state == LAST_ACK)
                t->state = CLOSED;
        }
        wake(t);   // room to write
    } else if ((flags & ACK) && ack == t->snd_una && dlen == 0 && !(flags & FIN) &&
               t->snd_una != t->snd_max && ++t->dupacks == 3 && t->snd.len > 0) {
        // the third duplicate ACK: the first segment got lost
        int n = t->snd.len < t->mss ? t->snd.len : t->mss;
        send_seg(t, t->snd_una, ACK, 0, n);
        t->timing = 0;
    }
    if ((flags & ACK) && SEQ_LEQ(t->snd_una, ack))
        t->snd_wnd = ntohs(h->win);

    // its data: only what comes next in order
    int ackit = 0;
    if (dlen > 0 || (flags & FIN)) {
        int skip = t->rcv_nxt - seq;
        if (skip > 0 && skip <= dlen) {
            // partly old: from the new part on
            data += skip;
            dlen -= skip;
            seq += skip;
        }
        ackit = 1;
        if (seq == t->rcv_nxt && (t->state == ESTABLISHED || t->state == FIN_WAIT_1 ||
                                  t->state == FIN_WAIT_2)) {
            int room = BUFSIZE - t->rcv.len, n = dlen < room ? dlen : room;
            for (int i = 0; i < n; i++)
                *ringat(&t->rcv, t->rcv.len + i) = data[i];
            t->rcv.len += n;
            t->rcv_nxt += n;
            // pieces that came early, and now follow on
            int grew = 0;
            for (int again = 1; again; ) {
                again = 0;
                for (int i = 0; i < t->nooo; i++) {
                    if (SEQ_LT(t->rcv_nxt, t->ooo[i].start))
                        continue;
                    if (SEQ_LT(t->rcv_nxt, t->ooo[i].end)) {
                        t->rcv.len += t->ooo[i].end - t->rcv_nxt;
                        t->rcv_nxt = t->ooo[i].end;
                        grew = 1;
                    }
                    t->ooo[i] = t->ooo[--t->nooo];
                    again = 1;
                    break;
                }
            }
            if ((flags & FIN) && n == dlen && t->nooo == 0) {
                t->rcv_nxt++;
                t->finrcvd = 1;
                if (t->state == ESTABLISHED)
                    t->state = CLOSE_WAIT;
                else if (t->state == FIN_WAIT_2)
                    enter_timewait(t);
                else if (t->state == FIN_WAIT_1)
                    t->state = CLOSING;
            }
            if (n > 0 || grew || t->finrcvd)
                wake(t);
        } else if (SEQ_LT(t->rcv_nxt, seq) && dlen > 0 && t->nooo < NOOO &&
                   (t->state == ESTABLISHED || t->state == FIN_WAIT_1 ||
                    t->state == FIN_WAIT_2)) {
            // early: keep it where it goes, if it fits
            int off = seq - t->rcv_nxt, room = BUFSIZE - t->rcv.len;
            if (off < room) {
                int n = dlen < room - off ? dlen : room - off;
                for (int i = 0; i < n; i++)
                    *ringat(&t->rcv, t->rcv.len + off + i) = data[i];
                t->ooo[t->nooo].start = seq;
                t->ooo[t->nooo].end = seq + n;
                t->nooo++;
            }
        }
    }
    if (t->state == TIME_WAIT && (flags & FIN))
        ackit = 1;   // its FIN again: our ACK got lost
    if (ackit)
        send_seg(t, t->snd_nxt, ACK, 0, 0);
    output(t);
    tcb_maybe_free(t);
    release(&t->lock);
}

// Every tick (clockintr()): retransmissions, probes, TIME_WAIT's end.
void tcp_tick(void)
{
    for (int i = 0; i < NTCB; i++) {
        struct tcb *t = &tcbs[i];
        if (__atomic_load_n(&t->deadline, __ATOMIC_RELAXED) == 0)
            continue;
        acquire(&t->lock);
        if (t->deadline == 0 || ticks < t->deadline) {
            release(&t->lock);
            continue;
        }
        t->deadline = 0;
        if (t->state == TIME_WAIT) {
            t->state = CLOSED;
            tcb_maybe_free(t);
        } else if (++t->tries > TCP_TRIES) {
            reset(t, -1);   // timed out
        } else {
            t->rto = t->rto * 2 < RTO_MAX ? t->rto * 2 : RTO_MAX;
            if (t->state == SYN_SENT) {
                send_seg(t, t->iss, SYN, 0, 0);
                t->timing = 0;
                arm(t);
            } else {
                // from the first unacknowledged byte on, FIN too
                t->snd_nxt = t->snd_una;
                t->timing = 0;
                t->finsent = 0;
                if (t->state == FIN_WAIT_1)
                    t->state = ESTABLISHED;
                else if (t->state == LAST_ACK)
                    t->state = CLOSE_WAIT;
                else if (t->state == CLOSING)
                    t->state = CLOSE_WAIT;   // FIN received; ours again
                t->probe = 1;
                output(t);
                t->probe = 0;
            }
        }
        release(&t->lock);
    }
}

// Connect from local port lport to rip:rport. Returns the connection,
// or 0 if refused, unreachable or interrupted.
struct tcb *tcp_connect(uint16 lport, uint32 rip, uint16 rport)
{
    if (net_arp_wait(rip) < 0)
        return 0;
    struct tcb *t = 0;
    for (int i = 0; i < NTCB && t == 0; i++) {
        acquire(&tcbs[i].lock);
        if (!tcbs[i].used) {
            t = &tcbs[i];
            t->used = 1;
            t->state = CLOSED;
            t->fileopen = 1;
        }
        release(&tcbs[i].lock);
    }
    if (t == 0)
        return 0;
    acquire(&t->lock);
    if (ring_alloc(&t->snd) < 0 || ring_alloc(&t->rcv) < 0) {
        t->fileopen = 0;
        tcb_maybe_free(t);
        release(&t->lock);
        return 0;
    }
    t->lport = lport;
    t->rport = rport;
    t->rip = rip;
    t->iss = timer_ns() >> 4;
    t->snd_una = t->iss;
    t->snd_nxt = t->iss + 1;
    t->snd_wnd = 0;
    t->rcv_nxt = 0;
    t->mss = 536;
    t->closing = t->finsent = t->finrcvd = t->err = 0;
    t->nooo = t->dupacks = t->srtt8 = t->rttvar4 = 0;
    t->tries = 0;
    t->rto = HZ;   // RFC 6298's first timeout
    t->deadline = 0;
    t->state = SYN_SENT;
    send_seg(t, t->iss, SYN, 0, 0);
    t->snd_max = t->snd_nxt;
    t->timing = 1;   // the handshake's round trip, too
    t->rtstart = ticks;
    arm(t);
    while (t->state == SYN_SENT && !interrupted(myproc()))
        sleep(t, &t->lock);
    if (t->state != ESTABLISHED) {
        if (t->state == SYN_SENT) {
            t->state = CLOSED;
            t->deadline = 0;
        }
        t->fileopen = 0;
        tcb_maybe_free(t);
        release(&t->lock);
        return 0;
    }
    release(&t->lock);
    return t;
}

// read(): up to n bytes into user address dst, waiting for some unless
// nonblock. 0 at the end (the peer's FIN), -1 on errors.
int tcp_read(struct tcb *t, uint64 dst, int n, int nonblock)
{
    acquire(&t->lock);
    while (t->rcv.len == 0) {
        if (t->finrcvd || t->err) {
            int r = t->err ? -1 : 0;
            release(&t->lock);
            return r;
        }
        if (nonblock || interrupted(myproc()) || t->state == CLOSED) {
            release(&t->lock);
            return -1;
        }
        sleep(t, &t->lock);
    }
    if (n > t->rcv.len)
        n = t->rcv.len;
    char *buf = kalloc();
    if (buf == 0) {
        release(&t->lock);
        return -1;
    }
    if (n > PGSIZE)
        n = PGSIZE;
    ring_copyout(&t->rcv, 0, buf, n);
    ring_drop(&t->rcv, n);
    // a window that opened again: tell the peer
    int room = BUFSIZE - t->rcv.len;
    if (t->adv < t->mss && room >= t->mss && t->state != CLOSED)
        send_seg(t, t->snd_nxt, ACK, 0, 0);
    release(&t->lock);
    int r = copyout(myproc()->pagetable, dst, buf, n) < 0 ? -1 : n;
    kfree(buf);
    return r;
}

// write(): n bytes from user address src, waiting for room. Returns n,
// or -1 if the connection is gone.
int tcp_write(struct tcb *t, uint64 src, int n)
{
    char *buf = kalloc();
    if (buf == 0)
        return -1;
    int done = 0;
    while (done < n) {
        int chunk = n - done < PGSIZE ? n - done : PGSIZE;
        if (copyin(myproc()->pagetable, buf, src + done, chunk) < 0)
            break;
        acquire(&t->lock);
        int i = 0;
        while (i < chunk) {
            if (t->err || t->closing || (t->state != ESTABLISHED && t->state != CLOSE_WAIT)) {
                release(&t->lock);
                kfree(buf);
                return done + i > 0 ? done + i : -1;
            }
            if (t->snd.len == BUFSIZE) {
                if (interrupted(myproc())) {
                    release(&t->lock);
                    kfree(buf);
                    return done + i > 0 ? done + i : -1;
                }
                sleep(t, &t->lock);
                continue;
            }
            *ringat(&t->snd, t->snd.len++) = buf[i++];
            if (t->snd.len == BUFSIZE || i == chunk)
                output(t);
        }
        release(&t->lock);
        done += chunk;
    }
    kfree(buf);
    return done;
}

// poll(): POLLIN with data, at the end or after an error; POLLOUT with
// room to write.
int tcp_poll(struct tcb *t)
{
    acquire(&t->lock);
    int r = 0;
    if (t->rcv.len > 0 || t->finrcvd || t->err || t->state == CLOSED)
        r |= POLLIN;
    if (t->snd.len < BUFSIZE || t->err || t->state == CLOSED)
        r |= POLLOUT;
    if (t->err || (t->finrcvd && t->state == CLOSED))
        r |= POLLHUP;
    release(&t->lock);
    return r;
}

// The socket is closed: FIN after the data still to send. The
// connection goes once that is done.
void tcp_close(struct tcb *t)
{
    acquire(&t->lock);
    t->fileopen = 0;
    t->closing = 1;
    output(t);
    tcb_maybe_free(t);
    release(&t->lock);
}
