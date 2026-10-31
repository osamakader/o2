// xHCI, the USB host controller on PCI: qemu's qemu-xhci, the Raspberry
// Pi 4's VL805. Enough of it for keyboards and mice plugged into its
// ports (no hubs): the devices are addressed and configured, and their
// HID boot interfaces' interrupt endpoints polled (usbhid.c).
//
// A kernel thread ("usb") does it all, polling the event ring every
// tick instead of taking interrupts, which PCI delivers differently on
// each machine. Rings and contexts are DMA pages (dma.c).

#include "types.h"
#include "param.h"
#include "memlayout.h"
#include "defs.h"
#include "console.h"
#include "spinlock.h"
#include "proc.h"
#include "timer.h"
#include "vm.h"
#include "mmu.h"
#include "pci.h"
#include "usb.h"

// capability registers
#define CAPLENGTH  0x00
#define HCSPARAMS1 0x04
#define HCSPARAMS2 0x08
#define HCCPARAMS1 0x10
#define DBOFF      0x14
#define RTSOFF     0x18
// operational registers
#define USBCMD   0x00
#define USBSTS   0x04
#define CRCR     0x18
#define DCBAAP   0x30
#define CONFIG   0x38
#define PORTSC(p) (0x400 + 0x10 * ((p) - 1))
// interrupter 0 (runtime registers)
#define ERSTSZ   0x28   // (from 0x20: IMAN, IMOD, which polling leaves alone)
#define ERSTBA   0x30
#define ERDP     0x38

#define CMD_RS    (1 << 0)
#define CMD_HCRST (1 << 1)
#define STS_HCH   (1 << 0)
#define STS_CNR   (1 << 11)

#define PORT_CCS (1 << 0)
#define PORT_PED (1 << 1)
#define PORT_PR  (1 << 4)
#define PORT_CSC (1 << 17)
#define PORT_PRC (1 << 21)
#define PORT_CHANGES 0x00fe0000            // the RW1C change bits, 17-23
#define PORT_KEEP    0x4e00c3e9            // bits that write back as they read
#define PORT_SPEED(s) (((s) >> 10) & 0xf)  // 1 full, 2 low, 3 high, 4 super

// TRBs
struct trb {
    uint64 param;
    uint32 status;
    uint32 ctrl;
};
#define TRB_NORMAL   1
#define TRB_SETUP    2
#define TRB_DATA     3
#define TRB_STATUS   4
#define TRB_LINK     6
#define TRB_ENABLE_SLOT  9
#define TRB_DISABLE_SLOT 10
#define TRB_ADDRESS_DEV  11
#define TRB_CONFIG_EP    12
#define TRB_EVAL_CTX     13
#define TRB_EV_TRANSFER  32
#define TRB_EV_COMMAND   33
#define TRB_EV_PORT      34
#define TRB_TYPE(t)   ((t) << 10)
#define TRB_GETTYPE(c) (((c) >> 10) & 0x3f)
#define TRB_CYCLE (1 << 0)
#define TRB_TC    (1 << 1)    // link: toggle the cycle
#define TRB_ISP   (1 << 2)    // interrupt (event) on a short packet
#define TRB_IOC   (1 << 5)
#define TRB_IDT   (1 << 6)    // setup: the data is in the TRB
#define TRB_DIR_IN (1 << 16)
#define CC_SUCCESS 1
#define CC_SHORT   13

#define NTRB (PGSIZE / sizeof(struct trb))

// a ring we produce TRBs on (commands, transfers)
struct ring {
    struct trb *trb;
    int i, cycle;
};

#define NSLOT 16

// a device: a slot, on a root port
static struct usbdev {
    int used;
    int port, speed;
    uint64 *ctx;          // output device context
    uint32 *in;           // input context
    struct ring ep0;
    uchar *buf;           // a page for control transfers
    // interrupt IN endpoints of HID interfaces: dci, ring, buffer
    int nep;
    struct {
        int dci, len;
        struct ring ring;
        uchar *buf;
        struct hid hid;
    } ep[2];
} devs[NSLOT + 1];        // by slot id, from 1

static uint64 cap, op, rt, db;
static int maxslots, maxports, csz;   // csz: context size in bytes
static uint64 *dcbaa;
static struct ring cmdring;
static struct trb *evring;
static int evi, evcycle = 1;
static int portdev[256];              // slot of the device on each port

static uint32 rd(uint64 base, int r) {
    return *(volatile uint32 *)(base + r);
}

static void wr(uint64 base, int r, uint32 v) {
    *(volatile uint32 *)(base + r) = v;
}

static void wr64(uint64 base, int r, uint64 v) {
    wr(base, r, v);
    wr(base, r + 4, v >> 32);
}

// Wait up to 1s (polling each tick) for (reg & mask) == want.
static int waitreg(uint64 base, int r, uint32 mask, uint32 want) {
    for (int t = 0; t < 100; t++) {
        if ((rd(base, r) & mask) == want)
            return 0;
        sleep_ticks(1);
    }
    return -1;
}

// Make r empty; its page is reused (a device plugged in again).
static int ring_init(struct ring *r) {
    if (r->trb == 0 && (r->trb = dma_alloc()) == 0)
        return -1;
    memset(r->trb, 0, PGSIZE);
    r->i = 0;
    r->cycle = 1;
    struct trb *link = &r->trb[NTRB - 1];
    link->param = dma_pa(r->trb);
    link->ctrl = TRB_TYPE(TRB_LINK) | TRB_TC;
    return 0;
}

// Put a TRB on r, the cycle bit last. Returns its physical address.
static uint64 ring_push(struct ring *r, uint64 param, uint32 status, uint32 ctrl) {
    struct trb *t = &r->trb[r->i];
    uint64 pa = dma_pa(t);
    t->param = param;
    t->status = status;
    dma_wmb();
    t->ctrl = ctrl | r->cycle;
    if (++r->i == NTRB - 1) {
        // past the end: through the link back to the start
        struct trb *link = &r->trb[NTRB - 1];
        dma_wmb();
        link->ctrl = TRB_TYPE(TRB_LINK) | TRB_TC | r->cycle;
        r->i = 0;
        r->cycle ^= 1;
    }
    dma_wmb();
    return pa;
}

static void doorbell(int slot, int target) {
    dma_wmb();
    wr(db, 4 * slot, target);
}

static void port_event(int port);
static void transfer_event(struct trb *e);

// The next event, into *e: 1, or 0 if there is none.
static int next_event(struct trb *e) {
    struct trb *t = &evring[evi];
    if ((t->ctrl & TRB_CYCLE) != evcycle)
        return 0;
    *e = *t;
    if (++evi == NTRB) {
        evi = 0;
        evcycle ^= 1;
    }
    wr64(rt, ERDP, dma_pa(&evring[evi]) | (1 << 3));   // and clear EHB
    return 1;
}

static void dispatch(struct trb *e) {
    switch (TRB_GETTYPE(e->ctrl)) {
    case TRB_EV_TRANSFER:
        transfer_event(e);
        break;
    case TRB_EV_PORT:
        port_event(e->param >> 24);
        break;
    }
}

// Wait up to 1s for the event a TRB at pa causes (command completion,
// or a transfer's), handling others meanwhile. Returns its completion
// code, -1 on timeout; the event goes in *out.
static int wait_for(uint64 pa, struct trb *out) {
    struct trb e;
    for (int t = 0; t < 100; ) {
        if (!next_event(&e)) {
            sleep_ticks(1);
            t++;
            continue;
        }
        int type = TRB_GETTYPE(e.ctrl);
        if ((type == TRB_EV_COMMAND || type == TRB_EV_TRANSFER) && e.param == pa) {
            if (out)
                *out = e;
            return e.status >> 24;
        }
        dispatch(&e);
    }
    return -1;
}

// Run a command; returns the completion code, the slot id in *slot.
static int command(uint64 param, uint32 ctrl, int *slot) {
    uint64 pa = ring_push(&cmdring, param, 0, ctrl);
    doorbell(0, 0);
    struct trb e;
    int cc = wait_for(pa, &e);
    if (slot)
        *slot = e.ctrl >> 24;
    return cc;
}

// A control transfer on d's endpoint 0, with len bytes of data in or
// out of d->buf. Returns the bytes transferred, or -1.
static int control(struct usbdev *d, int slot, int reqtype, int req, int value, int index,
                   int len) {
    int in = reqtype & 0x80;
    uint64 setup = reqtype | req << 8 | (uint64)value << 16 | (uint64)index << 32 |
                   (uint64)len << 48;
    ring_push(&d->ep0, setup, 8,
              TRB_TYPE(TRB_SETUP) | TRB_IDT | (len ? (in ? 3 : 2) << 16 : 0));
    if (len)
        ring_push(&d->ep0, dma_pa(d->buf), len, TRB_TYPE(TRB_DATA) | (in ? TRB_DIR_IN : 0));
    uint64 pa = ring_push(&d->ep0, 0, 0,
                          TRB_TYPE(TRB_STATUS) | TRB_IOC | (len && in ? 0 : TRB_DIR_IN));
    doorbell(slot, 1);
    struct trb e;
    int cc = wait_for(pa, &e);
    if (cc != CC_SUCCESS && cc != CC_SHORT)
        return -1;
    return len;
}

// the input context's parts: the control context, the slot context,
// and endpoint context dci
static uint32 *in_ctl(struct usbdev *d) {
    return d->in;
}

static uint32 *in_slot(struct usbdev *d) {
    return (uint32 *)((char *)d->in + csz);
}

static uint32 *in_ep(struct usbdev *d, int dci) {
    return (uint32 *)((char *)d->in + csz * (dci + 1));
}

// endpoint context: type (4 control, 7 interrupt IN), max packet size,
// its ring, interval (2^n x 125us)
static void ep_ctx(uint32 *ep, int type, int mps, struct ring *r, int interval) {
    memset(ep, 0, csz);
    ep[0] = interval << 16;
    ep[1] = 3 << 1 | type << 3 | mps << 16;   // 3 errors allowed
    uint64 pa = dma_pa(r->trb) | r->cycle;
    ep[2] = pa;
    ep[3] = pa >> 32;
    ep[4] = type == 4 ? 8 : (mps | mps << 16);   // average TRB length, max ESIT payload
}

// the xHCI interval for an interrupt endpoint's bInterval
static int interval(int speed, int binterval) {
    if (speed >= 3)   // high, super: 2^(bInterval-1) microframes
        return binterval < 1 ? 0 : binterval > 16 ? 15 : binterval - 1;
    // full, low: bInterval ms, as 2^n x 125us, n in 3..10
    int n = 3;
    while (n < 10 && (1 << (n - 3)) < binterval)
        n++;
    return n;
}

static struct usbdev *slotdev(int slot) {
    return slot >= 1 && slot <= NSLOT ? &devs[slot] : 0;
}

// Give the HID endpoint i of d a buffer for its next report.
static void queue_report(struct usbdev *d, int slot, int i) {
    ring_push(&d->ep[i].ring, dma_pa(d->ep[i].buf), d->ep[i].len,
              TRB_TYPE(TRB_NORMAL) | TRB_IOC | TRB_ISP);
    doorbell(slot, d->ep[i].dci);
}

static void transfer_event(struct trb *e) {
    int slot = e->ctrl >> 24, dci = (e->ctrl >> 16) & 0x1f, cc = e->status >> 24;
    struct usbdev *d = slotdev(slot);
    if (d == 0 || !d->used)
        return;
    for (int i = 0; i < d->nep; i++) {
        if (d->ep[i].dci != dci)
            continue;
        if (cc == CC_SUCCESS || cc == CC_SHORT)
            hid_report(&d->ep[i].hid, d->ep[i].buf, d->ep[i].len - (e->status & 0xffffff));
        if (cc == CC_SUCCESS || cc == CC_SHORT)
            queue_report(d, slot, i);
        else
            printf("usb: slot %d endpoint %d: completion code %d, stopped\n", slot, dci, cc);
    }
}

// Read the configuration, set it, and start polling its HID boot
// interfaces.
static int configure(struct usbdev *d, int slot) {
    if (control(d, slot, 0x80, USB_GET_DESCRIPTOR, USB_DESC_CONFIG << 8, 0, 9) < 0)
        return -1;
    int total = d->buf[2] | d->buf[3] << 8, value = d->buf[5];
    if (total > PGSIZE)
        total = PGSIZE;
    if (control(d, slot, 0x80, USB_GET_DESCRIPTOR, USB_DESC_CONFIG << 8, 0, total) < 0)
        return -1;

    // interfaces: HID boot keyboards and mice, and their interrupt IN
    // endpoints
    struct { int iface, proto, addr, mps, binterval; } found[2];
    int nfound = 0, iface = -1, proto = 0;
    for (int off = 0; off + 2 <= total && d->buf[off] >= 2; off += d->buf[off]) {
        uchar *p = d->buf + off;
        if (p[1] == USB_DESC_INTERFACE && off + 9 <= total) {
            iface = -1;
            if (p[5] == USB_CLASS_HID && p[6] == 1 && (p[7] == HID_KEYBOARD || p[7] == HID_MOUSE)) {
                iface = p[2];
                proto = p[7];
            }
        } else if (p[1] == USB_DESC_ENDPOINT && off + 7 <= total && iface >= 0 &&
                   (p[2] & 0x80) && (p[3] & 3) == 3 && nfound < 2) {
            found[nfound].iface = iface;
            found[nfound].proto = proto;
            found[nfound].addr = p[2] & 0xf;
            found[nfound].mps = (p[4] | p[5] << 8) & 0x7ff;
            found[nfound].binterval = p[6];
            nfound++;
            iface = -1;   // its first one only
        }
    }
    if (control(d, slot, 0x00, USB_SET_CONFIGURATION, value, 0, 0) < 0)
        return -1;
    if (nfound == 0)
        return 0;

    // boot protocol, and (keyboards) reports only on changes
    for (int i = 0; i < nfound; i++) {
        control(d, slot, 0x21, HID_SET_PROTOCOL, 0, found[i].iface, 0);
        if (found[i].proto == HID_KEYBOARD)
            control(d, slot, 0x21, HID_SET_IDLE, 0, found[i].iface, 0);
    }

    // their endpoints
    memset(d->in, 0, PGSIZE);
    int maxdci = 1;
    in_ctl(d)[1] = 1;   // add the slot context
    for (int i = 0; i < nfound; i++) {
        int dci = 2 * found[i].addr + 1;
        if (ring_init(&d->ep[i].ring) < 0 || (d->ep[i].buf == 0 && (d->ep[i].buf = dma_alloc()) == 0))
            return -1;
        d->ep[i].dci = dci;
        d->ep[i].len = found[i].mps > 64 ? 64 : found[i].mps;
        hid_init(&d->ep[i].hid, found[i].proto);
        in_ctl(d)[1] |= 1 << dci;
        ep_ctx(in_ep(d, dci), 7, found[i].mps, &d->ep[i].ring,
               interval(d->speed, found[i].binterval));
        if (dci > maxdci)
            maxdci = dci;
    }
    uint32 *s = in_slot(d);
    s[0] = d->speed << 20 | maxdci << 27;
    s[1] = d->port << 16;
    int cc = command(dma_pa(d->in), TRB_TYPE(TRB_CONFIG_EP) | slot << 24, 0);
    if (cc != CC_SUCCESS) {
        printf("usb: configure endpoints: completion code %d\n", cc);
        return -1;
    }
    d->nep = nfound;
    for (int i = 0; i < nfound; i++) {
        printf("usb: port %d: %s\n", d->port, found[i].proto == HID_KEYBOARD ? "keyboard" : "mouse");
        queue_report(d, slot, i);
    }
    return 0;
}

// A device was plugged into port: give it a slot and an address, and
// configure it.
static void attach(int port) {
    uint32 sc = rd(op, PORTSC(port));
    int speed = PORT_SPEED(sc);
    if (!(sc & PORT_PED)) {
        // reset the port (USB 2), which enables it
        wr(op, PORTSC(port), (sc & PORT_KEEP) | PORT_PR);
        if (waitreg(op, PORTSC(port), PORT_PRC, PORT_PRC) < 0)
            return;
        sc = rd(op, PORTSC(port));
        wr(op, PORTSC(port), (sc & PORT_KEEP) | PORT_PRC);
        if (!(sc & PORT_PED))
            return;
        speed = PORT_SPEED(sc);
    }
    int slot;
    if (command(0, TRB_TYPE(TRB_ENABLE_SLOT), &slot) != CC_SUCCESS || slotdev(slot) == 0) {
        printf("usb: port %d: no slot\n", port);
        return;
    }
    struct usbdev *d = slotdev(slot);
    if (d->ctx == 0 && ((d->ctx = dma_alloc()) == 0 || (d->in = dma_alloc()) == 0 ||
                        (d->buf = dma_alloc()) == 0))
        return;
    memset(d->ctx, 0, PGSIZE);
    if (ring_init(&d->ep0) < 0)
        return;
    d->used = 1;
    d->port = port;
    d->speed = speed;
    d->nep = 0;
    dcbaa[slot] = dma_pa(d->ctx);
    portdev[port] = slot;

    // address it, endpoint 0 at the speed's packet size
    int mps = speed == 4 ? 512 : speed == 3 ? 64 : 8;
    memset(d->in, 0, PGSIZE);
    in_ctl(d)[1] = 0x3;   // the slot and endpoint 0
    in_slot(d)[0] = speed << 20 | 1 << 27;
    in_slot(d)[1] = port << 16;
    ep_ctx(in_ep(d, 1), 4, mps, &d->ep0, 0);
    int cc = command(dma_pa(d->in), TRB_TYPE(TRB_ADDRESS_DEV) | slot << 24, 0);
    if (cc != CC_SUCCESS) {
        printf("usb: port %d: address device: completion code %d\n", port, cc);
        return;
    }
    // the real packet size of endpoint 0 (full speed: 8 to 64)
    if (control(d, slot, 0x80, USB_GET_DESCRIPTOR, USB_DESC_DEVICE << 8, 0, 8) < 0) {
        printf("usb: port %d: no device descriptor\n", port);
        return;
    }
    if (speed < 3 && d->buf[7] != mps) {
        mps = d->buf[7];
        memset(d->in, 0, PGSIZE);
        in_ctl(d)[1] = 0x2;
        ep_ctx(in_ep(d, 1), 4, mps, &d->ep0, 0);
        command(dma_pa(d->in), TRB_TYPE(TRB_EVAL_CTX) | slot << 24, 0);
    }
    if (control(d, slot, 0x80, USB_GET_DESCRIPTOR, USB_DESC_DEVICE << 8, 0, 18) < 0)
        return;
    printf("usb: port %d: %x:%x, class %d\n", port, d->buf[8] | d->buf[9] << 8,
           d->buf[10] | d->buf[11] << 8, d->buf[4]);
    if (d->buf[4] == USB_CLASS_HUB)
        printf("usb: port %d: hubs aren't supported; plug the device in directly\n", port);
    else if (configure(d, slot) < 0)
        printf("usb: port %d: configuring failed\n", port);
}

static void detach(int port) {
    int slot = portdev[port];
    struct usbdev *d = slotdev(slot);
    portdev[port] = 0;
    if (d == 0 || !d->used)
        return;
    d->used = 0;
    d->nep = 0;
    command(0, TRB_TYPE(TRB_DISABLE_SLOT) | slot << 24, 0);
    dcbaa[slot] = 0;
    printf("usb: port %d: unplugged\n", port);
}

static int pending[256];   // ports with a status change

static void port_event(int port) {
    if (port >= 1 && port <= maxports)
        pending[port] = 1;
}

// A port's connection changed (or at start): attach or detach.
static void port_check(int port) {
    uint32 sc = rd(op, PORTSC(port));
    wr(op, PORTSC(port), (sc & PORT_KEEP) | (sc & PORT_CHANGES));   // clear them
    if ((sc & PORT_CCS) && portdev[port] == 0)
        attach(port);
    else if (!(sc & PORT_CCS) && portdev[port])
        detach(port);
}

static int start(struct pci_dev *pd) {
    cap = P2V(pd->bar[0]);
    op = cap + (rd(cap, CAPLENGTH) & 0xff);
    rt = cap + (rd(cap, RTSOFF) & ~0x1f);
    db = cap + (rd(cap, DBOFF) & ~0x3);
    uint32 hcs1 = rd(cap, HCSPARAMS1), hcs2 = rd(cap, HCSPARAMS2);
    maxslots = (hcs1 & 0xff) < NSLOT ? (hcs1 & 0xff) : NSLOT;
    maxports = hcs1 >> 24;
    csz = rd(cap, HCCPARAMS1) & (1 << 2) ? 64 : 32;

    // stop it, and reset it
    if (waitreg(op, USBSTS, STS_CNR, 0) < 0)
        return -1;
    wr(op, USBCMD, rd(op, USBCMD) & ~CMD_RS);
    if (waitreg(op, USBSTS, STS_HCH, STS_HCH) < 0)
        return -1;
    wr(op, USBCMD, CMD_HCRST);
    if (waitreg(op, USBCMD, CMD_HCRST, 0) < 0 || waitreg(op, USBSTS, STS_CNR, 0) < 0)
        return -1;

    wr(op, CONFIG, maxslots);
    if ((dcbaa = dma_alloc()) == 0)
        return -1;
    // scratchpad pages, if it wants some
    int nscratch = (hcs2 >> 27 & 0x1f) | (hcs2 >> 21 & 0x1f) << 5;
    if (nscratch) {
        uint64 *array = dma_alloc();
        if (array == 0 || nscratch > PGSIZE / 8)
            return -1;
        for (int i = 0; i < nscratch; i++) {
            void *p = dma_alloc();
            if (p == 0)
                return -1;
            array[i] = dma_pa(p);
        }
        dcbaa[0] = dma_pa(array);
    }
    wr64(op, DCBAAP, dma_pa(dcbaa));
    if (ring_init(&cmdring) < 0)
        return -1;
    wr64(op, CRCR, dma_pa(cmdring.trb) | 1);

    // the event ring: one segment, one page
    uint64 *erst = dma_alloc();
    if (erst == 0 || (evring = dma_alloc()) == 0)
        return -1;
    erst[0] = dma_pa(evring);
    erst[1] = NTRB;
    wr(rt, ERSTSZ, 1);
    wr64(rt, ERDP, dma_pa(evring));
    wr64(rt, ERSTBA, dma_pa(erst));

    dma_wmb();
    wr(op, USBCMD, CMD_RS);
    if (waitreg(op, USBSTS, STS_HCH, 0) < 0)
        return -1;
    printf("usb: xhci %x:%x, %d ports, %d slots\n", pd->vendor, pd->device, maxports, maxslots);
    return 0;
}

static void usb_thread(void *arg) {
    struct pci_dev *pd = arg;
    pci_enable(pd);
    if (start(pd) < 0) {
        printf("usb: the controller didn't start\n");
        kthread_exit();
    }
    sleep_ticks(10);   // links coming up
    for (int p = 1; p <= maxports; p++)
        port_check(p);
    for (;;) {
        struct trb e;
        while (next_event(&e))
            dispatch(&e);
        for (int p = 1; p <= maxports; p++)
            if (pending[p]) {
                pending[p] = 0;
                port_check(p);
            }
        sleep_ticks(1);
    }
}

void xhci_init(void) {
    struct pci_dev *pd = pci_find(PCI_CLASS_XHCI);
    if (pd == 0 || pd->bar[0] == 0)
        return;
    struct proc *p = kthread_create(usb_thread, pd);
    if (p)
        safestrcpy(p->name, "usb", sizeof(p->name));
}
