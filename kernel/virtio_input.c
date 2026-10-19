//
// Driver for qemu's virtio input devices (keyboard, tablet, mouse), on
// the virtio-mmio transport like the disk. Each device sends evdev
// events through its event queue (queue 0), into buffers the driver
// keeps there; they go on to the input device (input.c). The status
// queue (LEDs) isn't used.
//
// qemu ... -device virtio-keyboard-device,bus=virtio-mmio-bus.3
//          -device virtio-tablet-device,bus=virtio-mmio-bus.4
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
#include "input.h"

#define VIRTIO_DEV_INPUT 18
#define NDEV_INPUT 4

// device configuration: select what to read, then read it
#define CFG_SELECT 0x00
#define CFG_SUBSEL 0x01
#define CFG_SIZE   0x02
#define CFG_DATA   0x08
#define CFG_ID_NAME 0x01

struct virtio_input_event {
    uint16 type;
    uint16 code;
    uint32 value;
};

static struct dev {
    uint64 base;       // kernel address of the registers
    int irq;
    struct spinlock lock;
    struct virtq_desc *desc;
    struct virtq_avail *avail;
    struct virtq_used *used;
    uint16 used_idx;
    struct virtio_input_event *ev;   // one per descriptor
} devs[NDEV_INPUT];
static int ndevs;

static volatile uint32 *reg(struct dev *d, int r)
{
    return (volatile uint32 *)(d->base + r);
}

static volatile uint8 *cfg(struct dev *d, int off)
{
    return (volatile uint8 *)(d->base + VIRTIO_MMIO_CONFIG + off);
}

static void *zpage(void)
{
    void *p = kalloc();
    if (p == 0)
        panic("virtio_input: kalloc");
    memset(p, 0, PGSIZE);
    return p;
}

// Give event buffer i to the device. d->lock held.
static void post(struct dev *d, int i)
{
    d->desc[i].addr = V2P(&d->ev[i]);
    d->desc[i].len = sizeof(struct virtio_input_event);
    d->desc[i].flags = VRING_DESC_F_WRITE;
    d->desc[i].next = 0;
    d->avail->ring[d->avail->idx % NUM] = i;
    __sync_synchronize();
    d->avail->idx += 1;
    __sync_synchronize();
}

static void intr(struct dev *d)
{
    acquire(&d->lock);
    *reg(d, VIRTIO_MMIO_INTERRUPT_ACK) = *reg(d, VIRTIO_MMIO_INTERRUPT_STATUS) & 0x3;
    __sync_synchronize();
    int posted = 0;
    while (d->used_idx != d->used->idx) {
        __sync_synchronize();
        int i = d->used->ring[d->used_idx % NUM].id;
        d->used_idx++;
        input_report(d->ev[i].type, d->ev[i].code, (int)d->ev[i].value);
        post(d, i);
        posted = 1;
    }
    if (posted)
        *reg(d, VIRTIO_MMIO_QUEUE_NOTIFY) = 0;
    release(&d->lock);
}

// one handler per device (irq_register() takes no argument)
static void intr0(void) { intr(&devs[0]); }
static void intr1(void) { intr(&devs[1]); }
static void intr2(void) { intr(&devs[2]); }
static void intr3(void) { intr(&devs[3]); }
static void (*intrs[NDEV_INPUT])(void) = { intr0, intr1, intr2, intr3 };

// Set up the device at slot (board.virtio). Returns 0, or -1.
static int setup(struct dev *d, int slot)
{
    uint32 status = 0;

    *reg(d, VIRTIO_MMIO_STATUS) = status;
    status |= VIRTIO_CONFIG_S_ACKNOWLEDGE | VIRTIO_CONFIG_S_DRIVER;
    *reg(d, VIRTIO_MMIO_STATUS) = status;
    *reg(d, VIRTIO_MMIO_DEVICE_FEATURES_SEL) = 1;
    if ((*reg(d, VIRTIO_MMIO_DEVICE_FEATURES) & (1 << (VIRTIO_F_VERSION_1 - 32))) == 0)
        return -1;
    *reg(d, VIRTIO_MMIO_DRIVER_FEATURES_SEL) = 0;
    *reg(d, VIRTIO_MMIO_DRIVER_FEATURES) = 0;
    *reg(d, VIRTIO_MMIO_DRIVER_FEATURES_SEL) = 1;
    *reg(d, VIRTIO_MMIO_DRIVER_FEATURES) = 1 << (VIRTIO_F_VERSION_1 - 32);
    status |= VIRTIO_CONFIG_S_FEATURES_OK;
    *reg(d, VIRTIO_MMIO_STATUS) = status;
    if (!(*reg(d, VIRTIO_MMIO_STATUS) & VIRTIO_CONFIG_S_FEATURES_OK))
        return -1;

    *reg(d, VIRTIO_MMIO_QUEUE_SEL) = 0;
    if (*reg(d, VIRTIO_MMIO_QUEUE_NUM_MAX) < NUM)
        return -1;
    d->desc = zpage();
    d->avail = zpage();
    d->used = zpage();
    d->ev = zpage();   // NUM events of 8 bytes
    *reg(d, VIRTIO_MMIO_QUEUE_NUM) = NUM;
    uint64 desc = V2P(d->desc), avail = V2P(d->avail), used = V2P(d->used);
    *reg(d, VIRTIO_MMIO_QUEUE_DESC_LOW) = desc;
    *reg(d, VIRTIO_MMIO_QUEUE_DESC_HIGH) = desc >> 32;
    *reg(d, VIRTIO_MMIO_DRIVER_DESC_LOW) = avail;
    *reg(d, VIRTIO_MMIO_DRIVER_DESC_HIGH) = avail >> 32;
    *reg(d, VIRTIO_MMIO_DEVICE_DESC_LOW) = used;
    *reg(d, VIRTIO_MMIO_DEVICE_DESC_HIGH) = used >> 32;
    *reg(d, VIRTIO_MMIO_QUEUE_READY) = 1;

    acquire(&d->lock);
    for (int i = 0; i < NUM; i++)
        post(d, i);
    release(&d->lock);
    status |= VIRTIO_CONFIG_S_DRIVER_OK;
    *reg(d, VIRTIO_MMIO_STATUS) = status;
    *reg(d, VIRTIO_MMIO_QUEUE_NOTIFY) = 0;

    // its name, for the boot log
    char name[32];
    *cfg(d, CFG_SELECT) = CFG_ID_NAME;
    *cfg(d, CFG_SUBSEL) = 0;
    int n = *cfg(d, CFG_SIZE);
    if (n > sizeof(name) - 1)
        n = sizeof(name) - 1;
    for (int i = 0; i < n; i++)
        name[i] = *cfg(d, CFG_DATA + i);
    name[n] = 0;
    d->irq = board.virtio[slot].irq;
    printf("virtio_input: slot %d, irq %d, %s\n", slot, d->irq, name);
    return 0;
}

void virtio_input_init(void)
{
    inputinit();
    for (int i = 0; i < board.nvirtio && ndevs < NDEV_INPUT; i++) {
        struct dev *d = &devs[ndevs];
        d->base = P2V(board.virtio[i].base);
        if (*reg(d, VIRTIO_MMIO_MAGIC_VALUE) != VIRTIO_MAGIC || *reg(d, VIRTIO_MMIO_VERSION) != 2 ||
            *reg(d, VIRTIO_MMIO_DEVICE_ID) != VIRTIO_DEV_INPUT)
            continue;
        initlock(&d->lock, "virtio_input");
        if (setup(d, i) < 0)
            continue;
        irq_register(d->irq, intrs[ndevs], cpuid());
        ndevs++;
    }
}
