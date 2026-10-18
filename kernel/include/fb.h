#ifndef FB_H
#define FB_H

#include "types.h"

// The framebuffer device ("fb", major FB), shared with user space.
// mmap() it (MAP_SHARED) to draw: pixels are uint32 0x00RRGGBB, rows
// stride bytes apart; then ioctl(fd, FB_FLUSH, &rect) to show a
// changed rectangle (0: all of it).

#define FB 2   // device major number

#define FB_INFO  2   // arg: struct fb_info *
#define FB_FLUSH 3   // arg: struct fb_rect *, or 0

struct fb_info {
    uint32 width, height;   // pixels
    uint32 stride;          // bytes per row
    uint32 size;            // bytes to map
};

struct fb_rect {
    uint32 x, y, width, height;
};

#endif
