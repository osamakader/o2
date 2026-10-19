// desk: the desktop, a small window manager on the framebuffer. Apps
// (term, files, view) are programs it starts; each draws its window
// in memory they share and tells desk what changed (win.h, libwin.c).
// desk draws the frames, the taskbar (buttons that start apps, one per
// window, a clock that opens the calendar) and the mouse pointer; windows move by their title
// bars and close with the box at the right of it.
//
// Keys come from /input (a keyboard), or, as lines, from the serial
// console, for boards without one; they go to the top window.
// Ctrl-Alt-Backspace, or a line "~." on the serial console, quits.

#include "types.h"
#include "user.h"
#include "fcntl.h"
#include "mman.h"
#include "fb.h"
#include "input.h"
#include "syscall.h"
#include "time.h"
#include "win.h"
#include "font.h"

#define NWIN      8
#define BORDER    2
#define TITLE_H   20
#define TASKBAR_H 24
#define CLOSE_W   18       // the close box, at the title bar's right
#define TASK_W    120      // a window's taskbar button

// colors
#define C_DESK_TOP  0x2a4a6a
#define C_DESK_BOT  0x0e1a2a
#define C_FRAME     0x50565e
#define C_TITLE     0x3a4048
#define C_TITLE_ACT 0x2f6fb8
#define C_CLOSE     0xc04040
#define C_TASKBAR   0x1c1f24
#define C_BUTTON    0x30353c
#define C_BUTTON_ACT 0x3d5a80
#define C_TEXT      0xf0f0f0

// the taskbar's buttons that start apps
static struct {
  char *label, *cmd;
  int x, w;
} launchers[] = {
  { "Terminal", "term" },
  { "Files", "files" },
  { "Calendar", "cal" },
};
#define NLAUNCH (sizeof(launchers) / sizeof(launchers[0]))

struct rect {
  int x, y, w, h;
};

// an app, and its window
struct win {
  int used;
  int pid;
  int ev, req, buf;   // events (we write), requests (we read), pixels
  int shown;          // has it asked to show its window?
  int closing;        // asked to close
  uint32 *px;         // its pixels, w x h
  int w, h;
  int x, y;           // the frame, on the screen
  char title[44];
};

static uint32 *fb, *back;   // the screen, and where we draw it
static struct fb_info info;
static int W, H;
static int fbfd;

static struct win wins[NWIN];
static int order[NWIN], nwin;   // shown windows, bottom to top; the top
                                // one has the keys
static int nlaunched;           // for where windows go

static struct rect clip;        // drawing stays inside
static struct rect dirty;       // what to draw again
static int mx, my, buttons;     // the mouse
static struct win *dragging;    // by its title bar
static struct win *grab;        // has the mouse while a button is down
static int dragx, dragy;
static int ctrl, shift, alt;
static int quit;

// ---- drawing, into back, inside clip

static void
damage(int x, int y, int w, int h)
{
  if(w <= 0 || h <= 0)
    return;
  if(dirty.w == 0){
    dirty = (struct rect){ x, y, w, h };
    return;
  }
  int x1 = dirty.x + dirty.w, y1 = dirty.y + dirty.h;
  if(x + w > x1)
    x1 = x + w;
  if(y + h > y1)
    y1 = y + h;
  if(x < dirty.x)
    dirty.x = x;
  if(y < dirty.y)
    dirty.y = y;
  dirty.w = x1 - dirty.x;
  dirty.h = y1 - dirty.y;
}

static void
fill(int x, int y, int w, int h, uint32 c)
{
  int x0 = x > clip.x ? x : clip.x, y0 = y > clip.y ? y : clip.y;
  int x1 = x + w < clip.x + clip.w ? x + w : clip.x + clip.w;
  int y1 = y + h < clip.y + clip.h ? y + h : clip.y + clip.h;
  for(int j = y0; j < y1; j++){
    uint32 *p = back + j * W;
    for(int i = x0; i < x1; i++)
      p[i] = c;
  }
}

static void
glyph(int x, int y, int ch, uint32 fg)
{
  if(ch <= ' ' || ch >= 127 || x + FONT_W <= clip.x || x >= clip.x + clip.w ||
     y + FONT_H <= clip.y || y >= clip.y + clip.h)
    return;
  const unsigned char *g = font[ch - 32];
  for(int j = 0; j < FONT_H; j++){
    int py = y + j;
    if(py < clip.y || py >= clip.y + clip.h)
      continue;
    for(int i = 0; i < FONT_W; i++){
      int px = x + i;
      if(px >= clip.x && px < clip.x + clip.w && (g[j] & (0x80 >> i)))
        back[py * W + px] = fg;
    }
  }
}

// s, at most max characters of it
static void
text(int x, int y, char *s, int max, uint32 c)
{
  for(; *s && max > 0; s++, max--, x += FONT_W)
    glyph(x, y, *s, c);
}

// ---- the scene

static int
framew(struct win *w)
{
  return w->w + 2 * BORDER;
}

static int
frameh(struct win *w)
{
  return TITLE_H + w->h + BORDER;
}

static struct win *
top(void)
{
  return nwin ? &wins[order[nwin - 1]] : 0;
}

static void
drawwin(struct win *w)
{
  int x = w->x, y = w->y, fw = framew(w);
  fill(x, y, fw, frameh(w), C_FRAME);
  fill(x + BORDER, y + BORDER, fw - 2 * BORDER, TITLE_H - BORDER, w == top() ? C_TITLE_ACT : C_TITLE);
  text(x + 8, y + BORDER + 1, w->title, (fw - 8 - CLOSE_W - 8) / FONT_W, C_TEXT);
  int bx = x + fw - BORDER - CLOSE_W, by = y + BORDER + 1;
  fill(bx, by, 16, 16, C_CLOSE);
  glyph(bx + 4, by, 'x', C_TEXT);

  // the app's pixels
  int cx = x + BORDER, cy = y + TITLE_H;
  int x0 = cx > clip.x ? cx : clip.x, y0 = cy > clip.y ? cy : clip.y;
  int x1 = cx + w->w < clip.x + clip.w ? cx + w->w : clip.x + clip.w;
  int y1 = cy + w->h < clip.y + clip.h ? cy + w->h : clip.y + clip.h;
  if(x1 > x0)
    for(int j = y0; j < y1; j++)
      memmove(back + j * W + x0, w->px + (j - cy) * w->w + (x0 - cx), (x1 - x0) * 4);
}

static void
drawtaskbar(void)
{
  int y = H - TASKBAR_H, x = 4;
  fill(0, y, W, TASKBAR_H, C_TASKBAR);
  for(int i = 0; i < NLAUNCH; i++){
    launchers[i].x = x;
    launchers[i].w = (strlen(launchers[i].label) + 2) * FONT_W;
    fill(x, y + 3, launchers[i].w, TASKBAR_H - 6, C_BUTTON);
    text(x + FONT_W, y + 4, launchers[i].label, 99, C_TEXT);
    x += launchers[i].w + 4;
  }
  // one button per window, in the order they were started
  x += 12;
  for(int k = 0; k < NWIN; k++){
    struct win *w = &wins[k];
    if(!w->used || !w->shown)
      continue;
    if(x + TASK_W > W - 18 * FONT_W)
      break;
    fill(x, y + 3, TASK_W, TASKBAR_H - 6, w == top() ? C_BUTTON_ACT : C_BUTTON);
    text(x + 6, y + 4, w->title, (TASK_W - 12) / FONT_W, C_TEXT);
    x += TASK_W + 4;
  }

  static char *days[] = { "Sun", "Mon", "Tue", "Wed", "Thu", "Fri", "Sat" };
  static char *months[] = { "Jan", "Feb", "Mar", "Apr", "May", "Jun",
                            "Jul", "Aug", "Sep", "Oct", "Nov", "Dec" };
  struct tm tm;
  gmtime(time(), &tm);
  // "Mon Oct 19 14:03"
  char s[17];
  memmove(s, days[tm.tm_wday], 3);
  s[3] = ' ';
  memmove(s + 4, months[tm.tm_mon], 3);
  s[7] = ' ';
  s[8] = tm.tm_mday >= 10 ? '0' + tm.tm_mday / 10 : ' ';
  s[9] = '0' + tm.tm_mday % 10;
  s[10] = ' ';
  s[11] = '0' + tm.tm_hour / 10;
  s[12] = '0' + tm.tm_hour % 10;
  s[13] = ':';
  s[14] = '0' + tm.tm_min / 10;
  s[15] = '0' + tm.tm_min % 10;
  s[16] = 0;
  text(W - 17 * FONT_W, y + 4, s, 16, C_TEXT);
}

// the mouse pointer: an arrow, # black, . white
static char *arrow[] = {
  "#",
  "##",
  "#.#",
  "#..#",
  "#...#",
  "#....#",
  "#.....#",
  "#......#",
  "#.......#",
  "#........#",
  "#.....#####",
  "#..#..#",
  "#.# #..#",
  "##  #..#",
  "#    #..#",
  "     #..#",
  "      ##",
};
#define ARROW_W 11
#define ARROW_H 17

// Draw r of the scene again and show it, with the pointer on top.
static void
redraw(struct rect r)
{
  if(r.x < 0){
    r.w += r.x;
    r.x = 0;
  }
  if(r.y < 0){
    r.h += r.y;
    r.y = 0;
  }
  if(r.x + r.w > W)
    r.w = W - r.x;
  if(r.y + r.h > H)
    r.h = H - r.y;
  if(r.w <= 0 || r.h <= 0)
    return;
  clip = r;

  // the background: a gradient down
  for(int y = r.y; y < r.y + r.h; y++){
    int t = y * 256 / H;
    uint32 c = 0;
    for(int s = 0; s < 24; s += 8){
      int a = (C_DESK_TOP >> s) & 0xff, b = (C_DESK_BOT >> s) & 0xff;
      c |= (uint32)(a + (b - a) * t / 256) << s;
    }
    fill(r.x, y, r.w, 1, c);
  }
  for(int i = 0; i < nwin; i++)
    drawwin(&wins[order[i]]);
  drawtaskbar();

  // to the screen
  int stride = info.stride / 4;
  for(int y = r.y; y < r.y + r.h; y++)
    memmove(fb + y * stride + r.x, back + y * W + r.x, r.w * 4);
  for(int j = 0; j < ARROW_H; j++)
    for(int i = 0; arrow[j][i]; i++){
      int x = mx + i, y = my + j;
      if(arrow[j][i] == ' ' || x < r.x || y < r.y || x >= r.x + r.w || y >= r.y + r.h)
        continue;
      fb[y * stride + x] = arrow[j][i] == '#' ? 0x000000 : 0xffffff;
    }
  struct fb_rect fr = { r.x, r.y, r.w, r.h };
  ioctl(fbfd, FB_FLUSH, (uint64)&fr);
}

static void
damagewin(struct win *w)
{
  damage(w->x, w->y, framew(w), frameh(w));
}

static void
damagetaskbar(void)
{
  damage(0, H - TASKBAR_H, W, TASKBAR_H);
}

// ---- apps

// Tell w's app; dropped if it isn't reading.
static void
event(struct win *w, int type, int x, int y, int a, int b)
{
  struct winmsg m;
  memset(&m, 0, sizeof(m));
  m.type = type;
  m.x = x;
  m.y = y;
  m.a = a;
  m.b = b;
  write(w->ev, &m, sizeof(m));
}

static void
raise(struct win *w)
{
  struct win *old = top();
  if(old == w)
    return;
  int k = w - wins, i;
  for(i = 0; order[i] != k; i++)
    ;
  for(; i < nwin - 1; i++)
    order[i] = order[i + 1];
  order[nwin - 1] = k;
  damagewin(w);
  damagetaskbar();
  if(old){
    damagewin(old);   // its title bar
    event(old, WE_FOCUS, 0, 0, 0, 0);
  }
  event(w, WE_FOCUS, 0, 0, 1, 0);
}

// Start the app in cmd ("name args...").
static void
launch(char *cmd)
{
  char buf[64], *argv[8];
  int argc = 0;
  strcpy(buf, "/");
  int n = strlen(cmd);
  if(n > sizeof(buf) - 2)
    n = sizeof(buf) - 2;
  memmove(buf + 1, cmd, n);
  buf[n + 1] = 0;
  for(char *s = buf; *s && argc < 7; ){
    while(*s == ' ')
      *s++ = 0;
    if(*s == 0)
      break;
    argv[argc++] = s;
    while(*s && *s != ' ')
      s++;
  }
  argv[argc] = 0;
  if(argc == 0)
    return;

  int k;
  for(k = 0; k < NWIN && wins[k].used; k++)
    ;
  if(k == NWIN)
    return;
  int ev[2], req[2], buf_;
  if(pipe(ev) < 0)
    return;
  if(pipe(req) < 0){
    close(ev[0]);
    close(ev[1]);
    return;
  }
  if((buf_ = memfd(W * H * 4)) < 0){
    close(ev[0]);
    close(ev[1]);
    close(req[0]);
    close(req[1]);
    return;
  }
  int pid = fork();
  if(pid == 0){
    // its three files at 3, 4, 5, and nothing else of ours
    int a = dup2(ev[0], 29), b = dup2(req[1], 30), c = dup2(buf_, 31);
    for(int fd = 3; fd < 29; fd++)
      close(fd);
    dup2(a, WIN_EVFD);
    dup2(b, WIN_REQFD);
    dup2(c, WIN_BUFFD);
    close(a);
    close(b);
    close(c);
    signal(SIGINT, SIG_DFL);
    signal(SIGPIPE, SIG_DFL);
    exec(argv[0], argv);
    fprintf(2, "desk: cannot run %s\n", argv[0]);
    exit(1);
  }
  close(ev[0]);
  close(req[1]);
  if(pid < 0){
    close(ev[1]);
    close(req[0]);
    close(buf_);
    return;
  }
  fcntl(ev[1], F_SETFL, O_NONBLOCK);
  fcntl(req[0], F_SETFL, O_NONBLOCK);

  struct win *w = &wins[k];
  memset(w, 0, sizeof(*w));
  w->used = 1;
  w->pid = pid;
  w->ev = ev[1];
  w->req = req[0];
  w->buf = buf_;
  // how big a window fits
  event(w, WE_SCREEN, 0, 0, W - 2 * BORDER - 32, H - TASKBAR_H - TITLE_H - BORDER - 32);
}

static void
hide(struct win *w)
{
  if(!w->shown)
    return;
  damagewin(w);
  damagetaskbar();
  int k = w - wins, i;
  for(i = 0; order[i] != k; i++)
    ;
  for(; i < nwin - 1; i++)
    order[i] = order[i + 1];
  nwin--;
  w->shown = 0;
  if(top()){
    damagewin(top());
    event(top(), WE_FOCUS, 0, 0, 1, 0);
  }
}

// w's app is gone (or going).
static void
forget(struct win *w)
{
  hide(w);
  if(dragging == w)
    dragging = 0;
  if(grab == w)
    grab = 0;
  if(w->px)
    munmap(w->px, w->w * w->h * 4);
  close(w->ev);
  close(w->req);
  close(w->buf);
  kill(w->pid, SIGKILL);
  waitpid(w->pid, 0, 0);
  w->used = 0;
}

static void
show(struct win *w, struct winmsg *m)
{
  int ww = m->a, wh = m->b;
  if(ww < 1 || wh < 1 || ww > W || wh > H || ww * wh > W * H)
    return;
  if(w->shown)
    damagewin(w);
  if(w->px)
    munmap(w->px, w->w * w->h * 4);
  w->px = mmap(0, ww * wh * 4, PROT_READ, MAP_SHARED, w->buf, 0);
  if(w->px == MAP_FAILED){
    w->px = 0;
    hide(w);
    return;
  }
  w->w = ww;
  w->h = wh;
  memmove(w->title, m->s, sizeof(w->title));
  w->title[sizeof(w->title) - 1] = 0;
  if(!w->shown){
    // cascading from the top left, as far as it fits
    int n = nlaunched++ % 8;
    w->x = 16 + n * 28;
    w->y = 16 + n * 28;
    if(w->x + framew(w) > W)
      w->x = W - framew(w) > 0 ? W - framew(w) : 0;
    if(w->y + frameh(w) > H - TASKBAR_H)
      w->y = H - TASKBAR_H - frameh(w) > 0 ? H - TASKBAR_H - frameh(w) : 0;
    w->shown = 1;
    order[nwin++] = w - wins;
    if(nwin > 1){
      // raise() it from just below the top
      order[nwin - 1] = order[nwin - 2];
      order[nwin - 2] = w - wins;
      raise(w);
    } else
      event(w, WE_FOCUS, 0, 0, 1, 0);
  }
  damagewin(w);
  damagetaskbar();
}

// What the apps asked for. Returns whether they asked anything.
static int
requests(void)
{
  struct winmsg m[8];
  int any = 0;
  for(int k = 0; k < NWIN; k++){
    struct win *w = &wins[k];
    if(!w->used)
      continue;
    int n = read(w->req, m, sizeof(m));
    if(n == 0){
      forget(w);   // it exited
      any = 1;
      continue;
    }
    for(int i = 0; i < n / (int)sizeof(m[0]); i++){
      any = 1;
      switch(m[i].type){
      case WR_SHOW:
        show(w, &m[i]);
        break;
      case WR_DAMAGE:
        if(w->shown)
          damage(w->x + BORDER + m[i].x, w->y + TITLE_H + m[i].y, m[i].a, m[i].b);
        break;
      case WR_TITLE:
        memmove(w->title, m[i].s, sizeof(w->title));
        w->title[sizeof(w->title) - 1] = 0;
        if(w->shown){
          damagewin(w);
          damagetaskbar();
        }
        break;
      case WR_LAUNCH:
        m[i].s[sizeof(m[i].s) - 1] = 0;
        launch(m[i].s);
        break;
      }
    }
  }
  return any;
}

// The close box: ask the app; the second time, insist.
static void
closewin(struct win *w)
{
  if(w->closing){
    forget(w);
    return;
  }
  w->closing = 1;
  event(w, WE_CLOSE, 0, 0, 0, 0);
}

// ---- input

// evdev key codes 0-57 to characters, US layout
static char keymap[58] =
  "\0\033" "1234567890-=" "\b\t" "qwertyuiop[]" "\n\0" "asdfghjkl;'`"
  "\0\\" "zxcvbnm,./" "\0*\0 ";
static char shiftmap[58] =
  "\0\033" "!@#$%^&*()_+" "\b\t" "QWERTYUIOP{}" "\n\0" "ASDFGHJKL:\"~"
  "\0|" "ZXCVBNM<>?" "\0*\0 ";

#define KEY_BACKSPACE 14
#define KEY_LEFTCTRL  29
#define KEY_LEFTSHIFT 42
#define KEY_RIGHTSHIFT 54
#define KEY_LEFTALT   56
#define KEY_RIGHTCTRL 97
#define KEY_RIGHTALT  100

// evdev codes of keys that aren't characters
static struct {
  int code, key;
} specials[] = {
  { 102, KEY_HOME }, { 103, KEY_UP }, { 104, KEY_PGUP }, { 105, KEY_LEFT },
  { 106, KEY_RIGHT }, { 107, KEY_END }, { 108, KEY_DOWN }, { 109, KEY_PGDN },
  { 111, KEY_DEL },
};

static void
typed(int c)
{
  if(top())
    event(top(), WE_KEY, 0, 0, c, 0);
}

static void
key(int code, int down)
{
  if(code == KEY_LEFTCTRL || code == KEY_RIGHTCTRL)
    ctrl = down;
  else if(code == KEY_LEFTSHIFT || code == KEY_RIGHTSHIFT)
    shift = down;
  else if(code == KEY_LEFTALT || code == KEY_RIGHTALT)
    alt = down;
  if(!down)
    return;
  if(ctrl && alt && code == KEY_BACKSPACE){
    quit = 1;
    return;
  }
  for(int i = 0; i < sizeof(specials) / sizeof(specials[0]); i++)
    if(specials[i].code == code){
      typed(specials[i].key);
      return;
    }
  if(code >= 58)
    return;
  int c = (shift ? shiftmap : keymap)[code];
  if(c == 0)
    return;
  if(ctrl && ((c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z')))
    c &= 0x1f;
  typed(c);
}

// the window whose app area is at (x, y), or 0
static struct win *
appat(int x, int y)
{
  for(int i = nwin - 1; i >= 0; i--){
    struct win *w = &wins[order[i]];
    if(x >= w->x && x < w->x + framew(w) && y >= w->y && y < w->y + frameh(w)){
      if(x >= w->x + BORDER && x < w->x + BORDER + w->w && y >= w->y + TITLE_H &&
         y < w->y + TITLE_H + w->h)
        return w;
      return 0;
    }
  }
  return 0;
}

static void
mouse(struct win *w, int kind, int a)
{
  event(w, WE_MOUSE, mx - w->x - BORDER, my - w->y - TITLE_H, a, kind);
}

static void
pointer(int x, int y)
{
  if(x < 0)
    x = 0;
  if(y < 0)
    y = 0;
  if(x >= W)
    x = W - 1;
  if(y >= H)
    y = H - 1;
  if(x == mx && y == my)
    return;
  damage(mx, my, ARROW_W, ARROW_H);
  if(dragging){
    damagewin(dragging);
    dragging->x = x - dragx;
    dragging->y = y - dragy;
    // keep the title bar where it can be grabbed
    if(dragging->y < 0)
      dragging->y = 0;
    if(dragging->y > H - TASKBAR_H - TITLE_H)
      dragging->y = H - TASKBAR_H - TITLE_H;
    damagewin(dragging);
  }
  mx = x;
  my = y;
  damage(mx, my, ARROW_W, ARROW_H);
  if(grab)
    mouse(grab, MOUSE_MOVE, buttons);
}

static void
taskbarclick(void)
{
  if(mx >= W - 18 * FONT_W){
    launch("cal");   // the clock
    return;
  }
  for(int i = 0; i < NLAUNCH; i++)
    if(mx >= launchers[i].x && mx < launchers[i].x + launchers[i].w){
      launch(launchers[i].cmd);
      return;
    }
  int x = launchers[NLAUNCH - 1].x + launchers[NLAUNCH - 1].w + 16;
  for(int k = 0; k < NWIN; k++){
    struct win *w = &wins[k];
    if(!w->used || !w->shown)
      continue;
    if(mx >= x && mx < x + TASK_W){
      raise(w);
      return;
    }
    x += TASK_W + 4;
  }
}

static void
press(int button)
{
  buttons |= button;
  if(grab){
    mouse(grab, MOUSE_DOWN, buttons);
    return;
  }
  if(my >= H - TASKBAR_H){
    if(button == 1)
      taskbarclick();
    return;
  }
  for(int i = nwin - 1; i >= 0; i--){
    struct win *w = &wins[order[i]];
    if(mx < w->x || mx >= w->x + framew(w) || my < w->y || my >= w->y + frameh(w))
      continue;
    raise(w);
    if(appat(mx, my) == w){
      grab = w;
      mouse(w, MOUSE_DOWN, buttons);
    } else if(button == 1 && my < w->y + TITLE_H){
      if(mx >= w->x + framew(w) - BORDER - CLOSE_W)
        closewin(w);
      else {
        dragging = w;
        dragx = mx - w->x;
        dragy = my - w->y;
      }
    }
    return;
  }
}

static void
release(int button)
{
  buttons &= ~button;
  if(button == 1)
    dragging = 0;
  if(grab){
    mouse(grab, MOUSE_UP, buttons);
    if(buttons == 0)
      grab = 0;
  }
}

// The input device's events. Returns whether there were any.
static int
readinput(int fd)
{
  struct input_event ev[16];
  int n = fd < 0 ? -1 : read(fd, ev, sizeof(ev));
  if(n <= 0)
    return 0;
  int x = mx, y = my, moved = 0;
  for(int i = 0; i < n / sizeof(ev[0]); i++){
    struct input_event *e = &ev[i];
    if(e->type == EV_KEY && e->code >= BTN_LEFT && e->code <= BTN_MIDDLE){
      if(moved){
        pointer(x, y);
        moved = 0;
      }
      int b = e->code == BTN_LEFT ? 1 : e->code == BTN_RIGHT ? 2 : 4;
      if(e->value)
        press(b);
      else
        release(b);
    } else if(e->type == EV_KEY){
      key(e->code, e->value != 0);
    } else if(e->type == EV_ABS && e->code == ABS_X){
      x = (long)e->value * W / (INPUT_ABS_MAX + 1);
      moved = 1;
    } else if(e->type == EV_ABS && e->code == ABS_Y){
      y = (long)e->value * H / (INPUT_ABS_MAX + 1);
      moved = 1;
    } else if(e->type == EV_REL && e->code == REL_X){
      x += e->value;
      moved = 1;
    } else if(e->type == EV_REL && e->code == REL_Y){
      y += e->value;
      moved = 1;
    } else if(e->type == EV_REL && e->code == REL_WHEEL){
      struct win *w = grab ? grab : appat(mx, my);
      if(w)
        mouse(w, MOUSE_WHEEL, e->value);
    }
  }
  if(moved)
    pointer(x, y);
  return 1;
}

// Lines typed on the serial console, as keys.
static int
readserial(int fd)
{
  char buf[128];
  int n = read(fd, buf, sizeof(buf));
  if(n <= 0)
    return 0;
  if(n >= 3 && buf[0] == '~' && buf[1] == '.' && (buf[2] == '\n' || buf[2] == '\r')){
    quit = 1;
    return 1;
  }
  for(int i = 0; i < n; i++)
    typed(buf[i]);
  return 1;
}

int
main(void)
{
  fbfd = open("/fb", O_RDWR);
  if(fbfd < 0 || ioctl(fbfd, FB_INFO, (uint64)&info) < 0){
    fprintf(2, "desk: no framebuffer\n");
    exit(1);
  }
  fb = mmap(0, info.size, PROT_READ | PROT_WRITE, MAP_SHARED, fbfd, 0);
  if(fb == MAP_FAILED){
    fprintf(2, "desk: mmap failed\n");
    exit(1);
  }
  W = info.width;
  H = info.height;
  back = malloc(W * H * 4);
  if(back == 0){
    fprintf(2, "desk: out of memory\n");
    exit(1);
  }
  int in = open("/input", O_RDONLY | O_NONBLOCK);
  // a file of its own, so the shell's stays as it was
  int ser = open("/console", O_RDONLY | O_NONBLOCK);
  // ^C on the console goes to everyone; and apps' pipes may close
  signal(SIGINT, SIG_IGN);
  signal(SIGPIPE, SIG_IGN);

  mx = W / 2;
  my = H / 2;
  launch("term");
  damage(0, 0, W, H);

  printf("desk: %dx%d; type ~. to quit\n", W, H);
  int minute = -1;
  while(!quit){
    int busy = readinput(in);
    busy |= readserial(ser);
    busy |= requests();
    int now = time() / 60;
    if(now != minute){
      minute = now;
      damagetaskbar();
    }
    if(dirty.w){
      redraw(dirty);
      dirty.w = 0;
    }
    if(!busy)
      pause(1);
  }

  for(int k = 0; k < NWIN; k++)
    if(wins[k].used)
      forget(&wins[k]);
  for(int y = 0; y < H; y++)
    memset(fb + y * (info.stride / 4), 0, W * 4);
  ioctl(fbfd, FB_FLUSH, 0);
  exit(0);
}
