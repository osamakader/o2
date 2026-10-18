// gfx [bounce [frames]]: draw on the framebuffer. Without arguments a
// test picture: a gradient, colored bars, a circle. bounce animates a
// square, showing only what changed each frame.

#include "types.h"
#include "user.h"
#include "fcntl.h"
#include "mman.h"
#include "fb.h"

static uint32 *fb;
static struct fb_info info;

static void
pixel(int x, int y, uint32 c)
{
  if(x >= 0 && y >= 0 && x < info.width && y < info.height)
    fb[y * (info.stride / 4) + x] = c;
}

static void
box(int x, int y, int w, int h, uint32 c)
{
  for(int j = y; j < y + h; j++)
    for(int i = x; i < x + w; i++)
      pixel(i, j, c);
}

static void
picture(void)
{
  // a gradient: red across, blue down
  for(int y = 0; y < info.height; y++)
    for(int x = 0; x < info.width; x++)
      pixel(x, y, (x * 255 / info.width) << 16 | (y * 255 / info.height));
  // bars along the top: white, red, green, blue, yellow, cyan, magenta, black
  uint32 bars[] = { 0xffffff, 0xff0000, 0x00ff00, 0x0000ff, 0xffff00, 0x00ffff, 0xff00ff, 0x000000 };
  int bw = info.width / 8;
  for(int i = 0; i < 8; i++)
    box(i * bw, 0, bw, info.height / 4, bars[i]);
  // a circle in the middle
  int cx = info.width / 2, cy = info.height * 5 / 8, r = info.height / 5;
  for(int y = -r; y <= r; y++)
    for(int x = -r; x <= r; x++)
      if(x * x + y * y <= r * r)
        pixel(cx + x, cy + y, 0xffa000);
}

static void
flush(int x, int y, int w, int h, int fd)
{
  struct fb_rect r = { x < 0 ? 0 : x, y < 0 ? 0 : y, w, h };
  ioctl(fd, FB_FLUSH, (uint64)&r);
}

static void
bounce(int fd, int frames)
{
  int s = 64, x = 0, y = 0, dx = 9, dy = 7;
  box(0, 0, info.width, info.height, 0x202040);
  ioctl(fd, FB_FLUSH, 0);
  for(int f = 0; f < frames; f++){
    box(x, y, s, s, 0x202040);
    int ox = x, oy = y;
    x += dx;
    y += dy;
    if(x < 0 || x + s > info.width){
      dx = -dx;
      x += 2 * dx;
    }
    if(y < 0 || y + s > info.height){
      dy = -dy;
      y += 2 * dy;
    }
    box(x, y, s, s, 0x40ff80);
    // what changed: the old square and the new one
    int x0 = ox < x ? ox : x, y0 = oy < y ? oy : y;
    flush(x0, y0, s + (ox > x ? ox - x : x - ox), s + (oy > y ? oy - y : y - oy), fd);
    pause(2);
  }
}

int
main(int argc, char *argv[])
{
  int fd = open("/fb", O_RDWR);
  if(fd < 0 || ioctl(fd, FB_INFO, (uint64)&info) < 0){
    fprintf(2, "gfx: no framebuffer\n");
    exit(1);
  }
  fb = mmap(0, info.size, PROT_READ | PROT_WRITE, MAP_SHARED, fd, 0);
  if(fb == MAP_FAILED){
    fprintf(2, "gfx: mmap failed\n");
    exit(1);
  }
  if(argc >= 2 && strcmp(argv[1], "bounce") == 0){
    bounce(fd, argc >= 3 ? atoi(argv[2]) : 200);
  } else {
    picture();
    ioctl(fd, FB_FLUSH, 0);
  }
  printf("gfx: %dx%d, done\n", info.width, info.height);
  exit(0);
}
