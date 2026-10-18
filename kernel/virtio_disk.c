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
#include "proc.h"
#include "intc.h"
#include "cpu.h"
#include "board.h"

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

// Find a virtio-mmio transport (board.virtio) with a modern (version 2)
// block device. Returns its index or -1.
static int probe(void)
{
    for (int i = 0; i < board.nvirtio; i++) {
        disk.base = P2V(board.virtio[i].base);
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

void virtio_disk_intr(void);

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
    disk.irq = board.virtio[slot].irq;

    printf("virtio_disk: slot %d, irq %d, %ld KB\n", slot, disk.irq,
           (long)(disk.capacity / 2));

    irq_register(disk.irq, virtio_disk_intr, cpuid());
}

// Is there a disk to read and write?
int virtio_disk_present(void)
{
    return disk.base != 0;
}

// find a free descriptor, mark it non-free, return its index.
static int alloc_desc(void)
{
    for (int i = 0; i < NUM; i++) {
        if (disk.free[i]) {
            disk.free[i] = 0;
            return i;
        }
    }
    return -1;
}

// mark a descriptor as free.
static void free_desc(int i)
{
    if (i >= NUM)
        panic("free_desc 1");
    if (disk.free[i])
        panic("free_desc 2");
    disk.desc[i].addr = 0;
    disk.desc[i].len = 0;
    disk.desc[i].flags = 0;
    disk.desc[i].next = 0;
    disk.free[i] = 1;
    wakeup(&disk.free[0]);
}

// free a chain of descriptors.
static void free_chain(int i)
{
    for (;;) {
        int flag = disk.desc[i].flags;
        int nxt = disk.desc[i].next;
        free_desc(i);
        if (flag & VRING_DESC_F_NEXT)
            i = nxt;
        else
            break;
    }
}

// allocate three descriptors (they need not be contiguous).
// disk transfers always use three descriptors.
static int alloc3_desc(int *idx)
{
    for (int i = 0; i < 3; i++) {
        idx[i] = alloc_desc();
        if (idx[i] < 0) {
            for (int j = 0; j < i; j++)
                free_desc(idx[j]);
            return -1;
        }
    }
    return 0;
}

// Put a request to read (write = 0) or write block b->blockno from/to
// b->data in descriptors idx[3] and the avail ring. disk.vdisk_lock
// held; the caller notifies the device.
static void submit(struct buf *b, int write, int *idx)
{
    uint64 sector = b->blockno * (BSIZE / 512);

    if (sector + BSIZE / 512 > disk.capacity)
        panic("virtio_disk_rw: block %d out of range", b->blockno);

    // the spec's Section 5.2 says that legacy block operations use
    // three descriptors: one for type/reserved/sector, one for the
    // data, one for a 1-byte status result.
    // qemu's virtio-blk.c reads them.

    struct virtio_blk_req *buf0 = &disk.ops[idx[0]];

    if (write)
        buf0->type = VIRTIO_BLK_T_OUT; // write the disk
    else
        buf0->type = VIRTIO_BLK_T_IN;  // read the disk
    buf0->reserved = 0;
    buf0->sector = sector;

    disk.desc[idx[0]].addr = V2P(buf0);
    disk.desc[idx[0]].len = sizeof(struct virtio_blk_req);
    disk.desc[idx[0]].flags = VRING_DESC_F_NEXT;
    disk.desc[idx[0]].next = idx[1];

    disk.desc[idx[1]].addr = V2P(b->data);
    disk.desc[idx[1]].len = BSIZE;
    if (write)
        disk.desc[idx[1]].flags = 0;                 // device reads b->data
    else
        disk.desc[idx[1]].flags = VRING_DESC_F_WRITE; // device writes b->data
    disk.desc[idx[1]].flags |= VRING_DESC_F_NEXT;
    disk.desc[idx[1]].next = idx[2];

    disk.info[idx[0]].status = 0xff; // device writes 0 on success
    disk.desc[idx[2]].addr = V2P(&disk.info[idx[0]].status);
    disk.desc[idx[2]].len = 1;
    disk.desc[idx[2]].flags = VRING_DESC_F_WRITE; // device writes the status
    disk.desc[idx[2]].next = 0;

    // record struct buf for virtio_disk_intr().
    b->disk = 1;
    disk.info[idx[0]].b = b;

    // tell the device the first index in our chain of descriptors.
    disk.avail->ring[disk.avail->idx % NUM] = idx[0];

    __sync_synchronize();

    // tell the device another avail ring entry is available.
    disk.avail->idx += 1; // not % NUM ...

    __sync_synchronize();
}

// Read (write = 0) or write the n blocks bufs[i]->blockno from/to
// their data, sleeping until the device is done with all of them. The
// requests go to the device together, up to NUM / 3 at a time. The
// caller must hold each buffer's lock (sleeplock).
void virtio_disk_rwv(struct buf **bufs, int n, int write)
{
    enum { BATCH = NUM / 3 };
    int idx[BATCH][3];

    if (disk.base == 0)
        panic("virtio_disk_rw: no disk");

    acquire(&disk.vdisk_lock);
    for (int done = 0; done < n; ) {
        int m = n - done < BATCH ? n - done : BATCH;
        for (int i = 0; i < m; i++) {
            // wait for descriptors that others' requests hold; ours
            // are only freed below
            while (alloc3_desc(idx[i]) != 0)
                sleep(&disk.free[0], &disk.vdisk_lock);
            submit(bufs[done + i], write, idx[i]);
        }
        *R(VIRTIO_MMIO_QUEUE_NOTIFY) = 0; // value is queue number

        // Wait for virtio_disk_intr() to say the requests have finished.
        for (int i = 0; i < m; i++) {
            struct buf *b = bufs[done + i];
            while (b->disk == 1)
                sleep(b, &disk.vdisk_lock);
            if (disk.info[idx[i][0]].status != 0)
                panic("virtio_disk_rw: status %d", disk.info[idx[i][0]].status);
            disk.info[idx[i][0]].b = 0;
            free_chain(idx[i][0]);
        }
        done += m;
    }
    release(&disk.vdisk_lock);
}

// Read (write = 0) or write block b->blockno from/to b->data, sleeping
// until the device is done. The caller must hold b->lock (sleeplock).
void virtio_disk_rw(struct buf *b, int write)
{
    virtio_disk_rwv(&b, 1, write);
}

void virtio_disk_intr(void)
{
    acquire(&disk.vdisk_lock);

    // the device won't raise another interrupt until we tell it we've
    // seen this interrupt, which the following line does. this may
    // race with the device writing new entries to the "used" ring, in
    // which case we may process the new completion entries in this
    // interrupt, and have nothing to do in the next interrupt, which
    // is harmless.
    *R(VIRTIO_MMIO_INTERRUPT_ACK) = *R(VIRTIO_MMIO_INTERRUPT_STATUS) & 0x3;

    __sync_synchronize();

    // the device increments disk.used->idx when it
    // adds an entry to the used ring.

    while (disk.used_idx != disk.used->idx) {
        __sync_synchronize();
        int id = disk.used->ring[disk.used_idx % NUM].id;

        if (disk.info[id].status != 0)
            panic("virtio_disk_intr status");

        struct buf *b = disk.info[id].b;
        b->disk = 0; // disk is done with buf
        wakeup(b);

        disk.used_idx += 1;
    }

    release(&disk.vdisk_lock);
}
