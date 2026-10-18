//
// Driver for qemu's virtio GPU, 2D only: a framebuffer in ordinary pages
// that the device shows on its first scanout. Programs map it through
// the fb device (mmap) and say which part changed (ioctl FB_FLUSH); the
// driver then has the device copy that part to its own resource and
// show it. Commands go through the control queue, waiting for each by
// polling: they also run at boot, before anything can sleep.
//
// qemu ... -device virtio-gpu-device,bus=virtio-mmio-bus.2
//

#include "types.h"
#include "param.h"
#include "memlayout.h"
#include "defs.h"
#include "console.h"
#include "spinlock.h"
#include "virtio.h"
#include "proc.h"
#include "fs.h"
#include "sleeplock.h"
#include "file.h"
#include "fcntl.h"
#include "board.h"
#include "fb.h"

#define R(r) ((volatile uint32 *)(gpu.base + (r)))

#define VIRTIO_DEV_GPU 16

// commands and responses
#define GET_DISPLAY_INFO     0x0100
#define RESOURCE_CREATE_2D   0x0101
#define SET_SCANOUT          0x0103
#define RESOURCE_FLUSH       0x0104
#define TRANSFER_TO_HOST_2D  0x0105
#define RESOURCE_ATTACH_BACKING 0x0106
#define RESP_OK_NODATA       0x1100
#define RESP_OK_DISPLAY_INFO 0x1101

#define FORMAT_B8G8R8X8 2   // a little-endian uint32 is 0x00RRGGBB
#define RESOURCE_ID 1
#define MAXPAGES 2048   // an 8MB framebuffer: 1920x1080 fits

struct ctrl_hdr {
    uint32 type;
    uint32 flags;
    uint64 fence_id;
    uint32 ctx_id;
    uint32 padding;
};

struct rect {
    uint32 x, y, width, height;
};

struct display_info {
    struct ctrl_hdr hdr;
    struct {
        struct rect r;
        uint32 enabled;
        uint32 flags;
    } pmodes[16];
};

struct create_2d {
    struct ctrl_hdr hdr;
    uint32 resource_id, format, width, height;
};

struct attach_backing {
    struct ctrl_hdr hdr;
    uint32 resource_id, nr_entries;
};

struct mem_entry {
    uint64 addr;
    uint32 length;
    uint32 padding;
};

struct set_scanout {
    struct ctrl_hdr hdr;
    struct rect r;
    uint32 scanout_id, resource_id;
};

struct transfer_2d {
    struct ctrl_hdr hdr;
    struct rect r;
    uint64 offset;
    uint32 resource_id, padding;
};

struct resource_flush {
    struct ctrl_hdr hdr;
    struct rect r;
    uint32 resource_id, padding;
};

static struct {
    uint64 base;   // registers; 0 if there is no GPU
    struct spinlock lock;
    struct virtq_desc *desc;
    struct virtq_avail *avail;
    struct virtq_used *used;
    uint16 used_idx;
    char *cmd;     // a page for a command, then its response at +2048
    struct fb_info info;
    char *pages[MAXPAGES];  // the framebuffer's pages (kernel addresses)
    int npages;
} gpu;

// Send a command made of n buffers (device-readable) and get its
// response into resp, len bytes. gpu.lock held. Returns the response
// type.
static uint32 command(void **bufs, uint32 *lens, int n, void *resp, uint32 len)
{
    for (int i = 0; i <= n; i++) {
        gpu.desc[i].addr = V2P(i < n ? bufs[i] : resp);
        gpu.desc[i].len = i < n ? lens[i] : len;
        gpu.desc[i].flags = i < n ? VRING_DESC_F_NEXT : VRING_DESC_F_WRITE;
        gpu.desc[i].next = i + 1;
    }
    ((struct ctrl_hdr *)resp)->type = 0;
    gpu.avail->ring[gpu.avail->idx % NUM] = 0;
    __sync_synchronize();
    gpu.avail->idx += 1;
    __sync_synchronize();
    *R(VIRTIO_MMIO_QUEUE_NOTIFY) = 0;
    for (uint64 spins = 0; gpu.used_idx == __atomic_load_n(&gpu.used->idx, __ATOMIC_ACQUIRE); spins++)
        if (spins > 1000000000UL)
            panic("virtio_gpu: no response");
    __sync_synchronize();
    gpu.used_idx++;
    return ((struct ctrl_hdr *)resp)->type;
}

// A command in one buffer, with a response header only.
static uint32 simple(void *req, uint32 len)
{
    void *bufs[1] = { req };
    uint32 lens[1] = { len };
    return command(bufs, lens, 1, gpu.cmd + 2048, sizeof(struct ctrl_hdr));
}

static void *zpage(void)
{
    void *p = kalloc();
    if (p == 0)
        panic("virtio_gpu: kalloc");
    memset(p, 0, PGSIZE);
    return p;
}

// Copy the rectangle (x, y, w, h) of the framebuffer to the device's
// resource and show it.
static void flush(uint32 x, uint32 y, uint32 w, uint32 h)
{
    struct transfer_2d *t = (struct transfer_2d *)gpu.cmd;
    memset(t, 0, sizeof(*t));
    t->hdr.type = TRANSFER_TO_HOST_2D;
    t->r = (struct rect){ x, y, w, h };
    t->offset = (uint64)y * gpu.info.stride + x * 4;
    t->resource_id = RESOURCE_ID;
    simple(t, sizeof(*t));

    struct resource_flush *f = (struct resource_flush *)gpu.cmd;
    memset(f, 0, sizeof(*f));
    f->hdr.type = RESOURCE_FLUSH;
    f->r = (struct rect){ x, y, w, h };
    f->resource_id = RESOURCE_ID;
    simple(f, sizeof(*f));
}

static int probe(void)
{
    for (int i = 0; i < board.nvirtio; i++) {
        gpu.base = P2V(board.virtio[i].base);
        if (*R(VIRTIO_MMIO_MAGIC_VALUE) == VIRTIO_MAGIC && *R(VIRTIO_MMIO_VERSION) == 2 &&
            *R(VIRTIO_MMIO_DEVICE_ID) == VIRTIO_DEV_GPU)
            return i;
    }
    gpu.base = 0;
    return -1;
}

static uint64 page(uint64 n);
static void flushrect(struct fb_rect *r);
static struct fbdev fbdev = { .page = page, .flush = flushrect };

void virtio_gpu_init(void)
{
    uint32 status = 0;

    initlock(&gpu.lock, "virtio_gpu");
    int slot = probe();
    if (slot < 0)
        return;

    *R(VIRTIO_MMIO_STATUS) = status;
    status |= VIRTIO_CONFIG_S_ACKNOWLEDGE | VIRTIO_CONFIG_S_DRIVER;
    *R(VIRTIO_MMIO_STATUS) = status;
    *R(VIRTIO_MMIO_DEVICE_FEATURES_SEL) = 1;
    if ((*R(VIRTIO_MMIO_DEVICE_FEATURES) & (1 << (VIRTIO_F_VERSION_1 - 32))) == 0) {
        gpu.base = 0;
        return;
    }
    *R(VIRTIO_MMIO_DRIVER_FEATURES_SEL) = 0;
    *R(VIRTIO_MMIO_DRIVER_FEATURES) = 0;   // no 3D, no EDID
    *R(VIRTIO_MMIO_DRIVER_FEATURES_SEL) = 1;
    *R(VIRTIO_MMIO_DRIVER_FEATURES) = 1 << (VIRTIO_F_VERSION_1 - 32);
    status |= VIRTIO_CONFIG_S_FEATURES_OK;
    *R(VIRTIO_MMIO_STATUS) = status;

    // the control queue
    *R(VIRTIO_MMIO_QUEUE_SEL) = 0;
    if (*R(VIRTIO_MMIO_QUEUE_NUM_MAX) < NUM)
        panic("virtio_gpu: queue too short");
    gpu.desc = zpage();
    gpu.avail = zpage();
    gpu.used = zpage();
    gpu.cmd = zpage();
    *R(VIRTIO_MMIO_QUEUE_NUM) = NUM;
    uint64 desc = V2P(gpu.desc), avail = V2P(gpu.avail), used = V2P(gpu.used);
    *R(VIRTIO_MMIO_QUEUE_DESC_LOW) = desc;
    *R(VIRTIO_MMIO_QUEUE_DESC_HIGH) = desc >> 32;
    *R(VIRTIO_MMIO_DRIVER_DESC_LOW) = avail;
    *R(VIRTIO_MMIO_DRIVER_DESC_HIGH) = avail >> 32;
    *R(VIRTIO_MMIO_DEVICE_DESC_LOW) = used;
    *R(VIRTIO_MMIO_DEVICE_DESC_HIGH) = used >> 32;
    *R(VIRTIO_MMIO_QUEUE_READY) = 1;
    status |= VIRTIO_CONFIG_S_DRIVER_OK;
    *R(VIRTIO_MMIO_STATUS) = status;

    acquire(&gpu.lock);

    // the first scanout's size
    struct ctrl_hdr *h = (struct ctrl_hdr *)gpu.cmd;
    memset(h, 0, sizeof(*h));
    h->type = GET_DISPLAY_INFO;
    void *bufs[16];
    uint32 lens[16];
    bufs[0] = h;
    lens[0] = sizeof(*h);
    struct display_info *di = (struct display_info *)(gpu.cmd + 2048);
    if (command(bufs, lens, 1, di, sizeof(*di)) != RESP_OK_DISPLAY_INFO || !di->pmodes[0].enabled) {
        release(&gpu.lock);
        printf("virtio_gpu: no display\n");
        gpu.base = 0;
        return;
    }
    gpu.info.width = di->pmodes[0].r.width;
    gpu.info.height = di->pmodes[0].r.height;
    gpu.info.stride = gpu.info.width * 4;
    gpu.info.size = PGROUNDUP((uint64)gpu.info.stride * gpu.info.height);

    // the framebuffer, in pages, and a list of them for the device
    gpu.npages = gpu.info.size / PGSIZE;
    int listpages = (gpu.npages * sizeof(struct mem_entry) + PGSIZE - 1) / PGSIZE;
    if (gpu.npages > MAXPAGES || listpages > NELEM(bufs) - 1)
        panic("virtio_gpu: display too big");
    struct mem_entry *list[16];
    for (int i = 0; i < listpages; i++)
        list[i] = zpage();
    for (int i = 0; i < gpu.npages; i++) {
        gpu.pages[i] = zpage();
        struct mem_entry *e = &list[i / (PGSIZE / sizeof(*e))][i % (PGSIZE / sizeof(*e))];
        e->addr = V2P(gpu.pages[i]);
        e->length = PGSIZE;
    }

    struct create_2d *c = (struct create_2d *)gpu.cmd;
    memset(c, 0, sizeof(*c));
    c->hdr.type = RESOURCE_CREATE_2D;
    c->resource_id = RESOURCE_ID;
    c->format = FORMAT_B8G8R8X8;
    c->width = gpu.info.width;
    c->height = gpu.info.height;
    uint32 r1 = simple(c, sizeof(*c));

    struct attach_backing *a = (struct attach_backing *)gpu.cmd;
    memset(a, 0, sizeof(*a));
    a->hdr.type = RESOURCE_ATTACH_BACKING;
    a->resource_id = RESOURCE_ID;
    a->nr_entries = gpu.npages;
    bufs[0] = a;
    lens[0] = sizeof(*a);
    for (int i = 0; i < listpages; i++) {
        bufs[i + 1] = list[i];
        int left = gpu.npages - i * (PGSIZE / sizeof(struct mem_entry));
        lens[i + 1] = (left < PGSIZE / sizeof(struct mem_entry) ? left : PGSIZE / sizeof(struct mem_entry)) *
                      sizeof(struct mem_entry);
    }
    uint32 r2 = command(bufs, lens, listpages + 1, gpu.cmd + 2048, sizeof(struct ctrl_hdr));
    for (int i = 0; i < listpages; i++)
        kfree(list[i]);

    struct set_scanout *s = (struct set_scanout *)gpu.cmd;
    memset(s, 0, sizeof(*s));
    s->hdr.type = SET_SCANOUT;
    s->r = (struct rect){ 0, 0, gpu.info.width, gpu.info.height };
    s->scanout_id = 0;
    s->resource_id = RESOURCE_ID;
    uint32 r3 = simple(s, sizeof(*s));
    if (r1 != RESP_OK_NODATA || r2 != RESP_OK_NODATA || r3 != RESP_OK_NODATA) {
        release(&gpu.lock);
        printf("virtio_gpu: setup failed (%x %x %x)\n", r1, r2, r3);
        gpu.base = 0;
        return;
    }
    flush(0, 0, gpu.info.width, gpu.info.height);
    release(&gpu.lock);

    printf("virtio_gpu: slot %d, %dx%d\n", slot, gpu.info.width, gpu.info.height);
    fbdev.info = gpu.info;
    fb_register(&fbdev);
}

// The physical address of framebuffer page n.
static uint64 page(uint64 n)
{
    return V2P(gpu.pages[n]);
}

static void flushrect(struct fb_rect *r)
{
    acquire(&gpu.lock);
    flush(r->x, r->y, r->width, r->height);
    release(&gpu.lock);
}
