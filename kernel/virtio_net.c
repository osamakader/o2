//
// Driver for qemu's virtio network device, through the mmio interface
// like the disk (virtio_disk.c). Queue 0 receives, queue 1 transmits.
// Each descriptor is one whole frame buffer, which starts with a
// virtio_net_hdr; no offloads are negotiated, so the header is zero
// going out and ignored coming in.
//
// qemu ... -netdev user,id=net0
//          -device virtio-net-device,netdev=net0,bus=virtio-mmio-bus.1
//

#include "types.h"
#include "param.h"
#include "memlayout.h"
#include "defs.h"
#include "console.h"
#include "spinlock.h"
#include "virtio.h"
#include "proc.h"
#include "intc.h"
#include "cpu.h"
#include "board.h"
#include "net.h"

#define R(r) ((volatile uint32 *)(net.base + (r)))

#define VIRTIO_DEV_NET     1
#define VIRTIO_NET_F_MAC   5    // the device has a MAC address in config

#define RXQ 0
#define TXQ 1
#define BUFSIZE 2048           // per descriptor: header + a frame

// virtio 1.0 network header (with VIRTIO_F_VERSION_1, num_buffers is
// always there)
struct virtio_net_hdr {
    uint8 flags;
    uint8 gso_type;
    uint16 hdr_len;
    uint16 gso_size;
    uint16 csum_start;
    uint16 csum_offset;
    uint16 num_buffers;
};

struct queue {
    struct virtq_desc *desc;
    struct virtq_avail *avail;
    struct virtq_used *used;
    uint16 used_idx;     // we've looked this far in used->ring
    char *buf[NUM];      // descriptor i's buffer
    char busy[NUM];      // tx: the device has descriptor i
};

static struct {
    uint64 base;   // kernel address of the transport's registers; 0 if none
    int irq;
    struct spinlock lock;
    struct queue q[2];
} net;

uchar net_mac[6];

static void *alloc_page(void)
{
    void *p = kalloc();
    if (p == 0)
        panic("virtio_net: kalloc");
    memset(p, 0, PGSIZE);
    return p;
}

// Set up queue n with NUM descriptors, each with a BUFSIZE buffer.
static void queue_init(int n)
{
    struct queue *q = &net.q[n];

    *R(VIRTIO_MMIO_QUEUE_SEL) = n;
    if (*R(VIRTIO_MMIO_QUEUE_READY))
        panic("virtio_net: queue ready");
    if (*R(VIRTIO_MMIO_QUEUE_NUM_MAX) < NUM)
        panic("virtio_net: queue too short");
    q->desc = alloc_page();
    q->avail = alloc_page();
    q->used = alloc_page();
    for (int i = 0; i < NUM; i += PGSIZE / BUFSIZE) {
        char *page = alloc_page();
        for (int j = 0; j < PGSIZE / BUFSIZE; j++)
            q->buf[i + j] = page + j * BUFSIZE;
    }
    *R(VIRTIO_MMIO_QUEUE_NUM) = NUM;
    uint64 desc = V2P(q->desc), avail = V2P(q->avail), used = V2P(q->used);
    *R(VIRTIO_MMIO_QUEUE_DESC_LOW) = desc;
    *R(VIRTIO_MMIO_QUEUE_DESC_HIGH) = desc >> 32;
    *R(VIRTIO_MMIO_DRIVER_DESC_LOW) = avail;
    *R(VIRTIO_MMIO_DRIVER_DESC_HIGH) = avail >> 32;
    *R(VIRTIO_MMIO_DEVICE_DESC_LOW) = used;
    *R(VIRTIO_MMIO_DEVICE_DESC_HIGH) = used >> 32;
    *R(VIRTIO_MMIO_QUEUE_READY) = 1;
}

// Give receive descriptor i to the device. net.lock held.
static void rx_post(int i)
{
    struct queue *q = &net.q[RXQ];
    q->desc[i].addr = V2P(q->buf[i]);
    q->desc[i].len = BUFSIZE;
    q->desc[i].flags = VRING_DESC_F_WRITE;
    q->desc[i].next = 0;
    q->avail->ring[q->avail->idx % NUM] = i;
    __sync_synchronize();
    q->avail->idx += 1;
    __sync_synchronize();
}

static int probe(void)
{
    for (int i = 0; i < board.nvirtio; i++) {
        net.base = P2V(board.virtio[i].base);
        if (*R(VIRTIO_MMIO_MAGIC_VALUE) == VIRTIO_MAGIC && *R(VIRTIO_MMIO_VERSION) == 2 &&
            *R(VIRTIO_MMIO_DEVICE_ID) == VIRTIO_DEV_NET)
            return i;
    }
    net.base = 0;
    return -1;
}

void virtio_net_intr(void);

void virtio_net_init(void)
{
    uint32 status = 0;

    initlock(&net.lock, "virtio_net");
    int slot = probe();
    if (slot < 0)
        return;   // no network

    *R(VIRTIO_MMIO_STATUS) = status;   // reset
    status |= VIRTIO_CONFIG_S_ACKNOWLEDGE;
    *R(VIRTIO_MMIO_STATUS) = status;
    status |= VIRTIO_CONFIG_S_DRIVER;
    *R(VIRTIO_MMIO_STATUS) = status;

    // features: the MAC address if there is one, and VERSION_1
    *R(VIRTIO_MMIO_DEVICE_FEATURES_SEL) = 0;
    uint32 lo = *R(VIRTIO_MMIO_DEVICE_FEATURES) & (1 << VIRTIO_NET_F_MAC);
    *R(VIRTIO_MMIO_DEVICE_FEATURES_SEL) = 1;
    if ((*R(VIRTIO_MMIO_DEVICE_FEATURES) & (1 << (VIRTIO_F_VERSION_1 - 32))) == 0) {
        printf("virtio_net: no VIRTIO_F_VERSION_1\n");
        net.base = 0;
        return;
    }
    *R(VIRTIO_MMIO_DRIVER_FEATURES_SEL) = 0;
    *R(VIRTIO_MMIO_DRIVER_FEATURES) = lo;
    *R(VIRTIO_MMIO_DRIVER_FEATURES_SEL) = 1;
    *R(VIRTIO_MMIO_DRIVER_FEATURES) = 1 << (VIRTIO_F_VERSION_1 - 32);
    status |= VIRTIO_CONFIG_S_FEATURES_OK;
    *R(VIRTIO_MMIO_STATUS) = status;
    if (!(*R(VIRTIO_MMIO_STATUS) & VIRTIO_CONFIG_S_FEATURES_OK))
        panic("virtio_net: FEATURES_OK unset");

    queue_init(RXQ);
    queue_init(TXQ);

    volatile uchar *config = (volatile uchar *)(net.base + VIRTIO_MMIO_CONFIG);
    for (int i = 0; i < 6; i++)
        net_mac[i] = lo ? config[i] : (i == 0 ? 0x52 : i);   // else a made-up local one

    acquire(&net.lock);
    for (int i = 0; i < NUM; i++)
        rx_post(i);
    release(&net.lock);

    status |= VIRTIO_CONFIG_S_DRIVER_OK;
    *R(VIRTIO_MMIO_STATUS) = status;
    *R(VIRTIO_MMIO_QUEUE_NOTIFY) = RXQ;

    net.irq = board.virtio[slot].irq;
    printf("virtio_net: slot %d, irq %d, mac %x:%x:%x:%x:%x:%x\n", slot, net.irq,
           net_mac[0], net_mac[1], net_mac[2], net_mac[3], net_mac[4], net_mac[5]);
    irq_register(net.irq, virtio_net_intr, cpuid());
}

int virtio_net_present(void)
{
    return net.base != 0;
}

// Take back transmit descriptors the device is done with. net.lock held.
static void tx_reclaim(void)
{
    struct queue *q = &net.q[TXQ];
    while (q->used_idx != q->used->idx) {
        __sync_synchronize();
        q->busy[q->used->ring[q->used_idx % NUM].id] = 0;
        q->used_idx++;
    }
}

// Send an Ethernet frame (destination, source, type, payload) of len
// bytes. Doesn't wait: returns 0, or -1 (dropped) if there is no
// network or every transmit buffer is in use.
int virtio_net_send(const void *frame, int len)
{
    struct queue *q = &net.q[TXQ];

    if (net.base == 0 || len > BUFSIZE - sizeof(struct virtio_net_hdr))
        return -1;
    acquire(&net.lock);
    tx_reclaim();
    int i;
    for (i = 0; i < NUM && q->busy[i]; i++)
        ;
    if (i == NUM) {
        release(&net.lock);
        return -1;
    }
    q->busy[i] = 1;
    memset(q->buf[i], 0, sizeof(struct virtio_net_hdr));
    memmove(q->buf[i] + sizeof(struct virtio_net_hdr), frame, len);
    q->desc[i].addr = V2P(q->buf[i]);
    q->desc[i].len = sizeof(struct virtio_net_hdr) + len;
    q->desc[i].flags = 0;
    q->desc[i].next = 0;
    q->avail->ring[q->avail->idx % NUM] = i;
    __sync_synchronize();
    q->avail->idx += 1;
    __sync_synchronize();
    *R(VIRTIO_MMIO_QUEUE_NOTIFY) = TXQ;
    release(&net.lock);
    return 0;
}

// Interrupt: hand received frames to net_rx(), outside net.lock since
// it may send replies, and give their buffers back to the device.
void virtio_net_intr(void)
{
    struct queue *q = &net.q[RXQ];

    acquire(&net.lock);
    *R(VIRTIO_MMIO_INTERRUPT_ACK) = *R(VIRTIO_MMIO_INTERRUPT_STATUS) & 0x3;
    __sync_synchronize();
    tx_reclaim();
    int posted = 0;
    while (q->used_idx != q->used->idx) {
        __sync_synchronize();
        struct virtq_used_elem *e = &q->used->ring[q->used_idx % NUM];
        int i = e->id, len = e->len;
        q->used_idx++;
        release(&net.lock);
        if (len > sizeof(struct virtio_net_hdr))
            net_rx(q->buf[i] + sizeof(struct virtio_net_hdr), len - sizeof(struct virtio_net_hdr));
        acquire(&net.lock);
        rx_post(i);
        posted = 1;
    }
    if (posted)
        *R(VIRTIO_MMIO_QUEUE_NOTIFY) = RXQ;
    release(&net.lock);
}
