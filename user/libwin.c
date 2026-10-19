// libwin: a window on desk's desktop, for apps (see win.h). Draw in
// win_px, then win_update() what changed.

#include "types.h"
#include "user.h"
#include "fcntl.h"
#include "mman.h"
#include "win.h"
#include "font.h"

int win_w, win_h;
uint32 *win_px;

static void
request(int type, int x, int y, int a, int b, char *s)
{
  struct winmsg m;
  memset(&m, 0, sizeof(m));
  m.type = type;
  m.x = x;
  m.y = y;
  m.a = a;
  m.b = b;
  if(s){
    int n = strlen(s);
    if(n > sizeof(m.s) - 1)
      n = sizeof(m.s) - 1;
    memmove(m.s, s, n);
  }
  write(WIN_REQFD, &m, sizeof(m));
}

// How big a window may be (desk says so first). Before win_open().
void
win_screen(int *w, int *h)
{
  struct winmsg m;
  *w = 640;
  *h = 400;
  if(read(WIN_EVFD, &m, sizeof(m)) == sizeof(m) && m.type == WE_SCREEN){
    *w = m.a;
    *h = m.b;
  }
}

// Show a w x h window. Returns 0, or -1 if not started by desk.
int
win_open(int w, int h, char *title)
{
  win_px = mmap(0, w * h * 4, PROT_READ | PROT_WRITE, MAP_SHARED, WIN_BUFFD, 0);
  if(win_px == MAP_FAILED){
    fprintf(2, "%s: needs the desktop (desk)\n", title);
    return -1;
  }
  win_w = w;
  win_h = h;
  fcntl(WIN_EVFD, F_SETFL, O_NONBLOCK);
  request(WR_SHOW, 0, 0, w, h, title);
  return 0;
}

void
win_fill(int x, int y, int w, int h, uint32 c)
{
  if(x < 0){
    w += x;
    x = 0;
  }
  if(y < 0){
    h += y;
    y = 0;
  }
  if(x + w > win_w)
    w = win_w - x;
  if(y + h > win_h)
    h = win_h - y;
  for(int j = y; j < y + h; j++)
    for(int i = x; i < x + w; i++)
      win_px[j * win_w + i] = c;
}

// A character's pixels, in fg; the rest stays.
void
win_char(int x, int y, int c, uint32 fg)
{
  if(c <= ' ' || c >= 127)
    return;
  const unsigned char *g = font[c - 32];
  for(int j = 0; j < FONT_H; j++){
    if(y + j < 0 || y + j >= win_h)
      continue;
    for(int i = 0; i < FONT_W; i++)
      if((g[j] & (0x80 >> i)) && x + i >= 0 && x + i < win_w)
        win_px[(y + j) * win_w + x + i] = fg;
  }
}

void
win_text(int x, int y, char *s, uint32 fg)
{
  for(; *s; s++, x += FONT_W)
    win_char(x, y, *s, fg);
}

void
win_update(int x, int y, int w, int h)
{
  request(WR_DAMAGE, x, y, w, h, 0);
}

void
win_title(char *s)
{
  request(WR_TITLE, 0, 0, 0, 0, s);
}

void
win_launch(char *cmd)
{
  request(WR_LAUNCH, 0, 0, 0, 0, cmd);
}

// The next event, into m: returns 1; or 0 if there is none (when not
// waiting), or -1 if desk is gone.
int
win_event(struct winmsg *m, int wait)
{
  if(wait)
    fcntl(WIN_EVFD, F_SETFL, 0);
  int n = read(WIN_EVFD, m, sizeof(*m));
  if(wait)
    fcntl(WIN_EVFD, F_SETFL, O_NONBLOCK);
  if(n == sizeof(*m))
    return 1;
  return n == 0 ? -1 : 0;
}
