// The framebuffer device (fb.h): one display, from whichever driver
// found one (virtio_gpu.c, rpi_fb.c) and registered it here. Programs
// mmap its pages and ask, through ioctl, for its geometry (FB_INFO) and
// to show a changed rectangle (FB_FLUSH).

#include "types.h"
#include "param.h"
#include "memlayout.h"
#include "defs.h"
#include "spinlock.h"
#include "sleeplock.h"
#include "proc.h"
#include "fs.h"
#include "file.h"
#include "fb.h"

static struct fbdev *fb;

static int fbioctl(int req, uint64 arg)
{
    struct proc *p = myproc();

    if (fb == 0)
        return -1;
    if (req == FB_INFO)
        return copyout(p->pagetable, arg, (char *)&fb->info, sizeof(fb->info));
    if (req != FB_FLUSH)
        return -1;
    struct fb_rect r = { 0, 0, fb->info.width, fb->info.height };
    if (arg && copyin(p->pagetable, (char *)&r, arg, sizeof(r)) < 0)
        return -1;
    if (r.x >= fb->info.width || r.y >= fb->info.height)
        return -1;
    if (r.width > fb->info.width - r.x)
        r.width = fb->info.width - r.x;
    if (r.height > fb->info.height - r.y)
        r.height = fb->info.height - r.y;
    if (r.width && r.height && fb->flush)
        fb->flush(&r);
    return 0;
}

static uint64 fbmmap(uint64 n)
{
    if (fb == 0 || n >= fb->info.size / PGSIZE)
        return 0;
    return fb->page(n);
}

// A driver found a display.
void fb_register(struct fbdev *dev)
{
    fb = dev;
    devsw[FB].ioctl = fbioctl;
    devsw[FB].mmap = fbmmap;
    devsw[FB].uncached = dev->uncached;
}
