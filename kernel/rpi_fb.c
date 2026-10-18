// The Raspberry Pi firmware's framebuffer, asked for through the
// mailbox property interface (rpi_mbox.c): the display's size, 32 bits
// per pixel in BGR order (a little-endian uint32 is 0x00RRGGBB, as
// virtio_gpu.c's), and memory the GPU scans out by itself, so flushing
// does nothing; it is mapped uncached so that writes reach it.

#include "types.h"
#include "param.h"
#include "memlayout.h"
#include "defs.h"
#include "console.h"
#include "board.h"
#include "fb.h"
#include "vm.h"

#define TAG_GET_PHYS_WH  0x00040003
#define TAG_SET_PHYS_WH  0x00048003
#define TAG_SET_VIRT_WH  0x00048004
#define TAG_SET_DEPTH    0x00048005
#define TAG_SET_PIXORDER 0x00048006
#define TAG_SET_VOFFSET  0x00048009
#define TAG_ALLOCATE     0x00040001
#define TAG_GET_PITCH    0x00040008

static uint32 msg[64];
static struct fbdev fbdev;
static uint64 fbbase;   // the framebuffer's physical address

static int call(void)
{
    return rpi_mbox_call(msg, NELEM(msg));
}

static uint64 page(uint64 n)
{
    return fbbase + n * PGSIZE;
}

void rpi_fb_init(void)
{
    if (board.mbox == 0)
        return;

    // the display's size, as the firmware set it up
    uint32 w = 0, h = 0;
    msg[0] = 8 * 4;
    msg[1] = 0;
    msg[2] = TAG_GET_PHYS_WH;
    msg[3] = 8;
    msg[4] = 0;
    msg[5] = msg[6] = 0;
    msg[7] = 0;
    if (call() == 0) {
        w = msg[5];
        h = msg[6];
    }
    if (w == 0 || h == 0) {
        w = 1280;
        h = 720;
    }

    int i = 2;
#define TAG(t, n) msg[i++] = (t), msg[i++] = 4 * (n), msg[i++] = 0
    TAG(TAG_SET_PHYS_WH, 2); msg[i++] = w; msg[i++] = h;
    TAG(TAG_SET_VIRT_WH, 2); msg[i++] = w; msg[i++] = h;
    TAG(TAG_SET_VOFFSET, 2); msg[i++] = 0; msg[i++] = 0;
    TAG(TAG_SET_DEPTH, 1); msg[i++] = 32;
    int order = i;
    TAG(TAG_SET_PIXORDER, 1); msg[i++] = 0;   // BGR
    int alloc = i;
    TAG(TAG_ALLOCATE, 2); msg[i++] = PGSIZE; msg[i++] = 0;
    int pitch = i;
    TAG(TAG_GET_PITCH, 1); msg[i++] = 0;
    msg[i++] = 0;   // end
    msg[0] = 4 * i;
    msg[1] = 0;
#undef TAG
    if (call() < 0 || msg[alloc + 3] == 0 || msg[order + 3] != 0) {
        printf("rpi_fb: no framebuffer\n");
        return;
    }
    fbbase = msg[alloc + 3] & 0x3fffffff;   // a GPU bus address
    fbdev.info.width = msg[5];
    fbdev.info.height = msg[6];
    fbdev.info.stride = msg[pitch + 3];
    fbdev.info.size = PGROUNDUP((uint64)fbdev.info.stride * fbdev.info.height);
    fbdev.page = page;
    fbdev.uncached = 1;
    printf("rpi_fb: %dx%d at %p\n", fbdev.info.width, fbdev.info.height, (void *)fbbase);
    fb_register(&fbdev);
}
