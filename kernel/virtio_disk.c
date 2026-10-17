//
// driver for qemu's virtio disk device. uses qemu's mmio interface to
// virtio. After xv6; the transport is found by probing the platform's
// virtio-mmio slots, and the device is handed physical addresses.
//
// qemu ... -drive file=fs.img,if=none,format=raw,id=x0
//          -device virtio-blk-device,drive=x0,bus=virtio-mmio-bus.0
//

#include "types.h"
#include "param.h"
#include "memlayout.h"
#include "defs.h"
#include "console.h"
#include "spinlock.h"
#include "buf.h"
#include "virtio.h"

// the address of virtio mmio register r.
#define R(r) ((volatile uint32 *)(disk.base + (r)))

static struct disk {
    uint64 base;  // kernel address of the transport's registers; 0 if none

    // a set (not a ring) of DMA descriptors, with which the driver
    // tells the device where to read and write individual disk
    // operations. there are NUM descriptors. most commands consist of
    // a "chain" (a linked list) of a couple of these descriptors.
    struct virtq_desc *desc;

    // a ring in which the driver writes descriptor numbers that the
    // driver would like the device to process. it only includes the
    // head descriptor of each chain. the ring has NUM elements.
    struct virtq_avail *avail;

    // a ring in which the device writes descriptor numbers that the
    // device has finished processing (just the head of each chain).
    // there are NUM used ring entries.
    struct virtq_used *used;

    // our own book-keeping.
    char free[NUM];  // is a descriptor free?
    uint16 used_idx; // we've looked this far in used[2..NUM].

    // track info about in-flight operations, for use when completion
    // interrupt arrives. indexed by first descriptor index of chain.
    struct {
        struct buf *b;
        char status;
    } info[NUM];

    // disk command headers. one-for-one with descriptors, for
    // convenience.
    struct virtio_blk_req ops[NUM];

    struct spinlock vdisk_lock;

    uint64 capacity;  // in 512-byte sectors
    int irq;
} disk;

// Find a virtio-mmio slot with a modern (version 2) block device.
// Returns its slot number or -1.
static int probe(void)
{
    for (int i = 0; i < VIRTIO_MMIO_COUNT; i++) {
        disk.base = P2V(VIRTIO_MMIO + i * VIRTIO_MMIO_STRIDE);
        if (*R(VIRTIO_MMIO_MAGIC_VALUE) == VIRTIO_MAGIC &&
            *R(VIRTIO_MMIO_VERSION) == 2 &&
            *R(VIRTIO_MMIO_DEVICE_ID) == VIRTIO_DEV_BLOCK)
            return i;
    }
    disk.base = 0;
    return -1;
}

static void *alloc_ring_page(void)
{
    void *p = kalloc();
    if (p == 0)
        panic("virtio disk kalloc");
    memset(p, 0, PGSIZE);
    return p;
}

void virtio_disk_init(void)
{
    uint32 status = 0;

    initlock(&disk.vdisk_lock, "virtio_disk");

    int slot = probe();
    if (slot < 0) {
        printf("virtio_disk: no disk\n");
        return;
    }

    // reset device
    *R(VIRTIO_MMIO_STATUS) = status;

    // set ACKNOWLEDGE status bit
    status |= VIRTIO_CONFIG_S_ACKNOWLEDGE;
    *R(VIRTIO_MMIO_STATUS) = status;

    // set DRIVER status bit
    status |= VIRTIO_CONFIG_S_DRIVER;
    *R(VIRTIO_MMIO_STATUS) = status;

    // negotiate features: none of the optional block features, just
    // VIRTIO_F_VERSION_1 (feature word 1, bit 0), required of a
    // modern driver.
    *R(VIRTIO_MMIO_DEVICE_FEATURES_SEL) = 1;
    uint32 hi = *R(VIRTIO_MMIO_DEVICE_FEATURES);
    if ((hi & (1 << (VIRTIO_F_VERSION_1 - 32))) == 0)
        panic("virtio disk: no VIRTIO_F_VERSION_1");
    *R(VIRTIO_MMIO_DRIVER_FEATURES_SEL) = 0;
    *R(VIRTIO_MMIO_DRIVER_FEATURES) = 0;
    *R(VIRTIO_MMIO_DRIVER_FEATURES_SEL) = 1;
    *R(VIRTIO_MMIO_DRIVER_FEATURES) = 1 << (VIRTIO_F_VERSION_1 - 32);

    // tell device that feature negotiation is complete.
    status |= VIRTIO_CONFIG_S_FEATURES_OK;
    *R(VIRTIO_MMIO_STATUS) = status;

    // re-read status to ensure FEATURES_OK is set.
    status = *R(VIRTIO_MMIO_STATUS);
    if (!(status & VIRTIO_CONFIG_S_FEATURES_OK))
        panic("virtio disk FEATURES_OK unset");

    // initialize queue 0.
    *R(VIRTIO_MMIO_QUEUE_SEL) = 0;

    // ensure queue 0 is not in use.
    if (*R(VIRTIO_MMIO_QUEUE_READY))
        panic("virtio disk should not be ready");

    // check maximum queue size.
    uint32 max = *R(VIRTIO_MMIO_QUEUE_NUM_MAX);
    if (max == 0)
        panic("virtio disk has no queue 0");
    if (max < NUM)
        panic("virtio disk max queue too short");

    // allocate and zero queue memory.
    disk.desc = alloc_ring_page();
    disk.avail = alloc_ring_page();
    disk.used = alloc_ring_page();

    // set queue size.
    *R(VIRTIO_MMIO_QUEUE_NUM) = NUM;

    // write physical addresses: the device does DMA, it doesn't know
    // about the kernel's high-half mapping.
    uint64 desc = V2P(disk.desc), avail = V2P(disk.avail), used = V2P(disk.used);
    *R(VIRTIO_MMIO_QUEUE_DESC_LOW) = desc;
    *R(VIRTIO_MMIO_QUEUE_DESC_HIGH) = desc >> 32;
    *R(VIRTIO_MMIO_DRIVER_DESC_LOW) = avail;
    *R(VIRTIO_MMIO_DRIVER_DESC_HIGH) = avail >> 32;
    *R(VIRTIO_MMIO_DEVICE_DESC_LOW) = used;
    *R(VIRTIO_MMIO_DEVICE_DESC_HIGH) = used >> 32;

    // queue is ready.
    *R(VIRTIO_MMIO_QUEUE_READY) = 0x1;

    // all NUM descriptors start out unused.
    for (int i = 0; i < NUM; i++)
        disk.free[i] = 1;

    // tell device we're completely ready.
    status |= VIRTIO_CONFIG_S_DRIVER_OK;
    *R(VIRTIO_MMIO_STATUS) = status;

    // config space: capacity in 512-byte sectors, 64 bits
    disk.capacity = R(VIRTIO_MMIO_CONFIG)[0] | ((uint64)R(VIRTIO_MMIO_CONFIG)[1] << 32);
    disk.irq = VIRTIO_MMIO_IRQ0 + slot;

    printf("virtio_disk: slot %d, irq %d, %ld KB\n", slot, disk.irq,
           (long)(disk.capacity / 2));
}
