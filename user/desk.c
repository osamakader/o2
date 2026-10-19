// desk: a small desktop on the framebuffer. Terminal windows, each
// running a shell through pipes; a taskbar with a button for a new
// terminal and a clock; a mouse pointer. Windows move by their title
// bars and close with the box at the right of it.
//
// Keys come from /input (a keyboard), or, as lines, from the serial
// console, for boards without one. A terminal echoes and edits the line
// itself and hands it to its shell with Enter: ^U kills the line, ^C
// interrupts what the shell runs, ^D on an empty line ends the shell.
// Ctrl-Alt-Backspace, or a line "~." on the serial console, quits.

#include "types.h"
#include "user.h"
#include "fcntl.h"
#include "mman.h"
#include "fb.h"
#include "input.h"
#include "procinfo.h"
#include "syscall.h"
#include "time.h"
#include "font.h"

#define NWIN     8
#define COLS     80
#define ROWS     24
#define BORDER   2
#define TITLE_H  20
#define PAD      4
#define TASKBAR_H 24
#define NEWTERM_W 104   // the taskbar's "new terminal" button

// colors
#define C_DESK_TOP  0x2a4a6a
#define C_DESK_BOT  0x0e1a2a
#define C_FRAME     0x50565e
#define C_TITLE     0x3a4048
#define C_TITLE_ACT 0x2f6fb8
#define C_CLOSE     0xc04040
#define C_TERM_BG   0x101214
#define C_TERM_FG   0xd8d8d8
#define C_TASKBAR   0x1c1f24
#define C_BUTTON    0x30353c
#define C_TEXT      0xf0f0f0

struct rect {
  int x, y, w, h;
};

struct win {
  int used;
  int x, y, w, h;           // the frame, on the screen
  char title[16];
  int pid;                  // its shell
  int in, out;              // the shell's stdin (we write) and stdout
  char text[ROWS][COLS];
  int cx, cy;               // the cursor
  int esc;                  // in an escape sequence the shell printed
  char line[128];           // being typed
  int len;
};

static uint32 *fb, *back;   // the screen, and where we draw it
static struct fb_info info;
static int W, H;
static int fbfd;
static int cols, rows;      // a terminal's size

static struct win wins[NWIN];
static int order[NWIN], nwin;   // windows bottom to top; the top one has the keys
static int nterms;              // ever opened, for titles

static struct rect clip;        // drawing stays inside
static struct rect dirty;       // what to draw again
static int mx, my, buttons;     // the mouse
static struct win *dragging;
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
glyph(int x, int y, int ch, uint32 fg, uint32 bg, int opaque)
{
  if(x + FONT_W <= clip.x || x >= clip.x + clip.w || y + FONT_H <= clip.y || y >= clip.y + clip.h)
    return;
  const unsigned char *g = ch >= 32 && ch < 127 ? font[ch - 32] : font[0];
  for(int j = 0; j < FONT_H; j++){
    int py = y + j;
    if(py < clip.y || py >= clip.y + clip.h)
      continue;
    for(int i = 0; i < FONT_W; i++){
      int px = x + i;
      if(px < clip.x || px >= clip.x + clip.w)
        continue;
      if(g[j] & (0x80 >> i))
        back[py * W + px] = fg;
      else if(opaque)
        back[py * W + px] = bg;
    }
  }
}

static void
text(int x, int y, char *s, uint32 c)
{
  for(; *s; s++, x += FONT_W)
    glyph(x, y, *s, c, 0, 0);
}

// ---- the scene

static int
termx(struct win *w)
{
  return w->x + BORDER + PAD;
}

static int
termy(struct win *w)
{
  return w->y + TITLE_H + PAD;
}

static void
drawwin(struct win *w, int active)
{
  fill(w->x, w->y, w->w, w->h, C_FRAME);
  fill(w->x + BORDER, w->y + BORDER, w->w - 2 * BORDER, TITLE_H - BORDER, active ? C_TITLE_ACT : C_TITLE);
  text(w->x + 8, w->y + BORDER + 1, w->title, C_TEXT);
  // the close box
  int bx = w->x + w->w - BORDER - 18, by = w->y + BORDER + 1;
  fill(bx, by, 16, 16, C_CLOSE);
  glyph(bx + 4, by, 'x', C_TEXT, 0, 0);
  // the terminal
  fill(w->x + BORDER, w->y + TITLE_H, w->w - 2 * BORDER, w->h - TITLE_H - BORDER, C_TERM_BG);
  int tx = termx(w), ty = termy(w);
  for(int r = 0; r < rows; r++)
    for(int c = 0; c < cols; c++)
      if(w->text[r][c] > ' ')
        glyph(tx + c * FONT_W, ty + r * FONT_H, w->text[r][c], C_TERM_FG, 0, 0);
  if(active){
    int ch = w->text[w->cy][w->cx];
    glyph(tx + w->cx * FONT_W, ty + w->cy * FONT_H, ch ? ch : ' ', C_TERM_BG, C_TERM_FG, 1);
  }
}

static void
drawtaskbar(void)
{
  int y = H - TASKBAR_H;
  fill(0, y, W, TASKBAR_H, C_TASKBAR);
  fill(4, y + 3, NEWTERM_W, TASKBAR_H - 6, C_BUTTON);
  text(12, y + 4, "+ terminal", C_TEXT);

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
  text(W - 17 * FONT_W, y + 4, s, C_TEXT);
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
    drawwin(&wins[order[i]], i == nwin - 1);
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
  damage(w->x, w->y, w->w, w->h);
}

// ---- terminals

static void
newline(struct win *w)
{
  w->cx = 0;
  if(++w->cy < rows)
    return;
  memmove(w->text[0], w->text[1], sizeof(w->text[0]) * (rows - 1));
  memset(w->text[rows - 1], 0, sizeof(w->text[0]));
  w->cy = rows - 1;
}

// Put a character the shell printed, or one typed, on the terminal.
static void
termput(struct win *w, int c)
{
  // escape sequences: ESC [ ... letter; dropped
  if(w->esc){
    if(w->esc == 1 && c != '[')
      w->esc = 0;
    else if(w->esc == 2 && c >= '@' && c <= '~')
      w->esc = 0;
    else
      w->esc = 2;
    return;
  }
  switch(c){
  case '\033':
    w->esc = 1;
    break;
  case '\n':
    newline(w);
    break;
  case '\r':
    w->cx = 0;
    break;
  case '\b':
    if(w->cx > 0)
      w->text[w->cy][--w->cx] = 0;
    break;
  case '\t':
    do
      termput(w, ' ');
    while(w->cx % 8);
    break;
  default:
    if(c < ' ' || c >= 127)
      break;
    if(w->cx == cols)
      newline(w);
    w->text[w->cy][w->cx++] = c;
  }
}

static struct win *
top(void)
{
  return nwin ? &wins[order[nwin - 1]] : 0;
}

static void
raise(struct win *w)
{
  int k = w - wins, i;
  for(i = 0; order[i] != k; i++)
    ;
  for(; i < nwin - 1; i++)
    order[i] = order[i + 1];
  order[nwin - 1] = k;
  damagewin(w);
  // the old top window's title bar and cursor change too
  if(nwin > 1)
    damagewin(&wins[order[nwin - 2]]);
}

static void
newterm(void)
{
  int k;
  for(k = 0; k < NWIN && wins[k].used; k++)
    ;
  if(k == NWIN)
    return;
  int toin[2], fromout[2];
  if(pipe(toin) < 0)
    return;
  if(pipe(fromout) < 0){
    close(toin[0]);
    close(toin[1]);
    return;
  }
  int pid = fork();
  if(pid == 0){
    dup2(toin[0], 0);
    dup2(fromout[1], 1);
    dup2(fromout[1], 2);
    // nothing else of ours: other shells must see their pipes end
    for(int fd = 3; fd < 16; fd++)
      close(fd);
    signal(SIGINT, SIG_DFL);
    signal(SIGPIPE, SIG_DFL);
    char *argv[] = { "sh", 0 };
    exec("/sh", argv);
    exit(1);
  }
  close(toin[0]);
  close(fromout[1]);
  if(pid < 0){
    close(toin[1]);
    close(fromout[0]);
    return;
  }
  fcntl(fromout[0], F_SETFL, O_NONBLOCK);

  struct win *w = &wins[k];
  memset(w, 0, sizeof(*w));
  w->used = 1;
  w->pid = pid;
  w->in = toin[1];
  w->out = fromout[0];
  w->w = cols * FONT_W + 2 * (BORDER + PAD);
  w->h = TITLE_H + rows * FONT_H + 2 * PAD + BORDER;
  w->x = 16 + (nterms % 6) * 28;
  w->y = 16 + (nterms % 6) * 28;
  nterms++;
  strcpy(w->title, "terminal ");
  w->title[9] = '0' + nterms % 10;
  order[nwin++] = k;
  raise(w);
}

static void
closewin(struct win *w)
{
  damagewin(w);
  if(dragging == w)
    dragging = 0;
  kill(w->pid, SIGKILL);
  close(w->in);
  close(w->out);
  waitpid(w->pid, 0, 0);
  w->used = 0;
  int k = w - wins, i;
  for(i = 0; order[i] != k; i++)
    ;
  for(; i < nwin - 1; i++)
    order[i] = order[i + 1];
  nwin--;
  if(nwin)
    damagewin(top());
}

// SIGINT to what w's shell runs, and the shell (which prompts again)
static void
interrupt(struct win *w)
{
  static struct procinfo ps[64];
  int n = getprocs(ps, 64);
  for(int i = 0; i < n; i++){
    // is w's shell an ancestor (or itself)?
    int pid = ps[i].pid;
    for(int hops = 0; pid && pid != w->pid && hops < n; hops++){
      int j;
      for(j = 0; j < n && ps[j].pid != pid; j++)
        ;
      pid = j < n ? ps[j].ppid : 0;
    }
    if(pid == w->pid && ps[i].state != PS_ZOMBIE)
      kill(ps[i].pid, SIGINT);
  }
}

// A key typed into w.
static void
termkey(struct win *w, int c)
{
  damagewin(w);
  if(c == ('C' & 0x1f)){
    termput(w, '^');
    termput(w, 'C');
    termput(w, '\n');
    w->len = 0;
    interrupt(w);
  } else if(c == ('D' & 0x1f)){
    if(w->len == 0){
      close(w->in);   // the shell reads its end, and exits
      w->in = -1;
    }
  } else if(c == ('U' & 0x1f)){
    while(w->len > 0){
      w->len--;
      termput(w, '\b');
    }
  } else if(c == '\b' || c == 127){
    if(w->len > 0){
      w->len--;
      termput(w, '\b');
    }
  } else if(c == '\n' || c == '\r'){
    termput(w, '\n');
    w->line[w->len++] = '\n';
    if(w->in >= 0)
      write(w->in, w->line, w->len);
    w->len = 0;
  } else if((c >= ' ' && c < 127) || c == '\t'){
    if(w->len < sizeof(w->line) - 1){
      w->line[w->len++] = c;
      termput(w, c);
    }
  }
}

// What the shells printed. Returns whether there was anything.
static int
readterms(void)
{
  char buf[256];
  int any = 0;
  for(int k = 0; k < NWIN; k++){
    struct win *w = &wins[k];
    if(!w->used)
      continue;
    int n = read(w->out, buf, sizeof(buf));
    if(n == 0){
      closewin(w);   // the shell exited
      any = 1;
      continue;
    }
    if(n < 0)
      continue;
    for(int i = 0; i < n; i++)
      termput(w, buf[i]);
    damagewin(w);
    any = 1;
  }
  return any;
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

static void
key(int code, int down)
{
  if(code == KEY_LEFTCTRL || code == KEY_RIGHTCTRL)
    ctrl = down;
  else if(code == KEY_LEFTSHIFT || code == KEY_RIGHTSHIFT)
    shift = down;
  else if(code == KEY_LEFTALT || code == KEY_RIGHTALT)
    alt = down;
  if(!down || code >= 58)
    return;
  if(ctrl && alt && code == KEY_BACKSPACE){
    quit = 1;
    return;
  }
  int c = (shift ? shiftmap : keymap)[code];
  if(c == 0)
    return;
  if(ctrl && ((c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z')))
    c &= 0x1f;
  if(top())
    termkey(top(), c);
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
}

static void
click(void)
{
  if(my >= H - TASKBAR_H){
    if(mx >= 4 && mx < 4 + NEWTERM_W)
      newterm();
    return;
  }
  for(int i = nwin - 1; i >= 0; i--){
    struct win *w = &wins[order[i]];
    if(mx < w->x || mx >= w->x + w->w || my < w->y || my >= w->y + w->h)
      continue;
    if(i != nwin - 1)
      raise(w);
    if(my < w->y + TITLE_H){
      if(mx >= w->x + w->w - BORDER - 18)
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
      if(e->code == BTN_LEFT){
        if(moved){
          pointer(x, y);
          moved = 0;
        }
        if(e->value)
          click();
        else
          dragging = 0;
        buttons = e->value;
      }
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
  if(top())
    for(int i = 0; i < n; i++)
      termkey(top(), buf[i]);
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
  // ^C on the console goes to everyone; and shells' pipes may close
  signal(SIGINT, SIG_IGN);
  signal(SIGPIPE, SIG_IGN);

  cols = (W - 2 * (BORDER + PAD) - 48) / FONT_W;
  if(cols > COLS)
    cols = COLS;
  rows = (H - TASKBAR_H - TITLE_H - 2 * PAD - BORDER - 48) / FONT_H;
  if(rows > ROWS)
    rows = ROWS;
  mx = W / 2;
  my = H / 2;
  newterm();
  damage(0, 0, W, H);

  printf("desk: %dx%d; type ~. to quit\n", W, H);
  int minute = -1;
  while(!quit){
    int busy = readinput(in);
    busy |= readserial(ser);
    busy |= readterms();
    int now = time() / 60;
    if(now != minute){
      minute = now;
      damage(0, H - TASKBAR_H, W, TASKBAR_H);
    }
    if(dirty.w){
      redraw(dirty);
      dirty.w = 0;
    }
    if(!busy)
      pause(1);
  }

  while(nwin)
    closewin(top());
  memset(back, 0, W * H * 4);
  for(int y = 0; y < H; y++)
    memset(fb + y * (info.stride / 4), 0, W * 4);
  ioctl(fbfd, FB_FLUSH, 0);
  exit(0);
}
