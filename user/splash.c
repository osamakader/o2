// splash: the boot animation. init runs it next to the shell. The o2
// logo, drawn from shapes (a ring for the o; an arc and two strokes
// for the 2) with smooth edges, fades in and grows while a bar fills
// below it; the last frame stays. Without a framebuffer it does
// nothing.

#include "types.h"
#include "user.h"
#include "fcntl.h"
#include "mman.h"
#include "fb.h"

#define FRAMES 40

static uint32 *fb;
static struct fb_info info;
static int fd;

static float
clamp(float x, float lo, float hi)
{
  return x < lo ? lo : x > hi ? hi : x;
}

static float
fsqrt(float x)
{
  return __builtin_sqrtf(x);   // one instruction, with -fno-math-errno
}

// atan2(y, x) in radians, to within about 0.005
static float
fatan2(float y, float x)
{
  float ax = x < 0 ? -x : x, ay = y < 0 ? -y : y;
  if(ax == 0 && ay == 0)
    return 0;
  float a = ax > ay ? ay / ax : ax / ay;
  float s = a * a;
  float r = ((-0.0464964749f * s + 0.15931422f) * s - 0.327622764f) * s * a + a;
  if(ay > ax)
    r = 1.57079637f - r;
  if(x < 0)
    r = 3.14159274f - r;
  return y < 0 ? -r : r;
}

// Distance from (px, py) to the segment (ax, ay)-(bx, by).
static float
segment(float px, float py, float ax, float ay, float bx, float by)
{
  float dx = bx - ax, dy = by - ay;
  float t = clamp(((px - ax) * dx + (py - ay) * dy) / (dx * dx + dy * dy), 0, 1);
  float ex = px - ax - t * dx, ey = py - ay - t * dy;
  return fsqrt(ex * ex + ey * ey);
}

// The 2's arc: radius R around (CX, CY), from angle A0 to A1 (radians,
// y down: increasing angles go clockwise), over the top.
#define CX 0.62f
#define CY (-0.17f)
#define R 0.28f
#define A0 (-3.14159274f)
#define A1 0.70f
static float arc_x0, arc_y0, arc_x1, arc_y1;   // its ends

// Distance from (px, py) to the arc, if less than best; else best.
static float
arc(float px, float py, float best)
{
  float dx = px - CX, dy = py - CY;
  float ring = fsqrt(dx * dx + dy * dy) - R;
  if(ring < 0)
    ring = -ring;
  if(ring >= best)
    return best;   // the arc is no nearer than its circle
  float a = fatan2(dy, dx);
  if(a >= A0 && a <= A1){
    float d = fsqrt(dx * dx + dy * dy) - R;
    return d < 0 ? -d : d;
  }
  float d0 = fsqrt((px - arc_x0) * (px - arc_x0) + (py - arc_y0) * (py - arc_y0));
  float d1 = fsqrt((px - arc_x1) * (px - arc_x1) + (py - arc_y1) * (py - arc_y1));
  if(d1 < d0)
    d0 = d1;
  return d0 < best ? d0 : best;
}

// The logo's strokes, in units where it spans about [-0.9, 0.9] x
// [-0.55, 0.55]: the distance from (x, y) to the nearest one.
static float
logo(float x, float y)
{
  x += 0.08f;
  // o: a ring sitting on the baseline
  float dx = x + 0.45f, dy = y - 0.17f;
  float d = fsqrt(dx * dx + dy * dy) - 0.30f;
  d = d < 0 ? -d : d;
  // 2: an arc over the top, a diagonal, the base
  float d3 = segment(x, y, arc_x1, arc_y1, 0.34f, 0.47f);
  float d4 = segment(x, y, 0.34f, 0.47f, 0.92f, 0.47f);
  if(d3 < d)
    d = d3;
  if(d4 < d)
    d = d4;
  return arc(x, y, d);
}

static uint32
mix(uint32 a, uint32 b, float t)
{
  int r = ((a >> 16) & 0xff) * (1 - t) + ((b >> 16) & 0xff) * t;
  int g = ((a >> 8) & 0xff) * (1 - t) + ((b >> 8) & 0xff) * t;
  int bl = (a & 0xff) * (1 - t) + (b & 0xff) * t;
  return r << 16 | g << 8 | bl;
}

// the background: dark blue, lighter toward the middle
static uint32
background(int x, int y)
{
  float dx = (x - info.width / 2.0f) / info.width, dy = (y - info.height / 2.0f) / info.height;
  float v = clamp(1 - 2.2f * (dx * dx + dy * dy) * 2, 0, 1);
  return mix(0x05070f, 0x1a2348, v);
}

// One frame, t from 0 to 1, in the box around the logo; returns it.
static struct fb_rect
frame(float t)
{
  float ease = 1 - (1 - t) * (1 - t) * (1 - t);
  float unit = info.height / 5.0f;           // pixels per logo unit at full size
  float scale = unit * (0.75f + 0.25f * ease);
  int cx = info.width / 2, cy = info.height / 2 - unit * 0.3f;

  struct fb_rect box;
  int hw = unit * 1.1f, top = unit * 0.7f, bottom = unit * 1.2f;
  box.x = cx - hw;
  box.y = cy - top;
  box.width = 2 * hw;
  box.height = top + bottom;

  float half = 0.085f;                       // stroke half-width
  for(int py = box.y; py < box.y + box.height; py++){
    for(int px = box.x; px < box.x + box.width; px++){
      uint32 c = background(px, py);
      float x = (px - cx) / scale, y = (py - cy) / scale;
      // the logo: coverage from the distance, over about a pixel; none
      // well above or below it
      if(y > -0.7f && y < 0.7f){
        float d = logo(x, y);
        float cover = clamp(0.5f - (d - half) * scale, 0, 1) * ease;
        if(cover > 0)
          c = mix(c, mix(0x00d0ff, 0xb050ff, clamp((x + 0.9f) / 1.8f, 0, 1)), cover);
      }
      // the bar below: a track, filling up
      float by = (py - cy) / unit - 0.95f, bx = (px - cx) / unit;
      if(bx > -0.8f && bx < 0.8f && by > -0.02f && by < 0.02f)
        c = mix(c, bx < -0.8f + 1.6f * t ? 0x60c8ff : 0x2a3560, 0.9f);
      fb[py * (info.stride / 4) + px] = c;
    }
  }
  return box;
}

int
main(void)
{
  if((fd = open("/fb", O_RDWR)) < 0 || ioctl(fd, FB_INFO, (uint64)&info) < 0)
    exit(0);
  fb = mmap(0, info.size, PROT_READ | PROT_WRITE, MAP_SHARED, fd, 0);
  if(fb == MAP_FAILED)
    exit(0);
  // constants: the compiler works these out
  arc_x0 = CX + R * __builtin_cosf(A0);
  arc_y0 = CY + R * __builtin_sinf(A0);
  arc_x1 = CX + R * __builtin_cosf(A1);
  arc_y1 = CY + R * __builtin_sinf(A1);

  for(int y = 0; y < info.height; y++)
    for(int x = 0; x < info.width; x++)
      fb[y * (info.stride / 4) + x] = background(x, y);
  ioctl(fd, FB_FLUSH, 0);

  for(int f = 0; f <= FRAMES; f++){
    struct fb_rect r = frame((float)f / FRAMES);
    ioctl(fd, FB_FLUSH, (uint64)&r);
    pause(2);
  }
  exit(0);
}
