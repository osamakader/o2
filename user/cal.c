// cal: a calendar on the desktop. A month at a time, today marked;
// < and > (or the arrows, Page Up/Down, the wheel) change the month,
// Today (or Home) goes back. Click a day to select it.

#include "types.h"
#include "user.h"
#include "time.h"
#include "win.h"

#define CELL_W  48
#define CELL_H  36
#define PAD     8
#define HEAD_H  40    // the month and its buttons
#define WDAY_H  24    // the weekdays' names
#define FOOT_H  24    // the date and time
#define BTN_W   28
#define TODAY_W 56

#define C_BG     0xf6f6f4
#define C_TEXT   0x202428
#define C_DIM    0xb0b4b8
#define C_HEAD   0x2f6fb8
#define C_WEEKND 0xc04040
#define C_TODAY  0x2f6fb8
#define C_SEL    0xd8e4f4
#define C_BUTTON 0xe0e2e6
#define C_FOOT   0xe0e2e6

static char *months[] = { "January", "February", "March", "April", "May", "June", "July",
                          "August", "September", "October", "November", "December" };
static char *wdays[] = { "Su", "Mo", "Tu", "We", "Th", "Fr", "Sa" };
static char *wdaynames[] = { "Sunday", "Monday", "Tuesday", "Wednesday", "Thursday",
                             "Friday", "Saturday" };

static int year, mon;          // shown: the year, the month 0..11
static int sely, selm, seld;   // the selected day, if seld
static struct tm now;

static int
leap(int y)
{
  return (y % 4 == 0 && y % 100 != 0) || y % 400 == 0;
}

static int
mdays(int y, int m)
{
  static int n[] = { 31, 28, 31, 30, 31, 30, 31, 31, 30, 31, 30, 31 };
  return m == 1 && leap(y) ? 29 : n[m];
}

// the weekday (0 Sunday) of y-m-d
static int
wday(int y, int m, int d)
{
  struct tm tm = { .tm_year = y - 1900, .tm_mon = m, .tm_mday = d };
  struct tm out;
  gmtime(timegm(&tm), &out);
  return out.tm_wday;
}

static char *
num(char *s, int v)
{
  char d[12];
  int n = 0;
  do
    d[n++] = '0' + v % 10;
  while((v /= 10) > 0);
  while(n > 0)
    *s++ = d[--n];
  *s = 0;
  return s;
}

static char *
cat(char *s, char *t)
{
  strcpy(s, t);
  return s + strlen(t);
}

static void
center(int x, int w, int y, char *s, uint32 c)
{
  win_text(x + (w - (int)strlen(s) * FONT_W) / 2, y, s, c);
}

static void
draw(void)
{
  char s[64], *e;
  win_fill(0, 0, win_w, win_h, C_BG);

  // the month, between < and >, and Today
  win_fill(PAD, PAD, BTN_W, 24, C_BUTTON);
  center(PAD, BTN_W, PAD + 4, "<", C_TEXT);
  int rx = win_w - PAD - BTN_W;
  win_fill(rx, PAD, BTN_W, 24, C_BUTTON);
  center(rx, BTN_W, PAD + 4, ">", C_TEXT);
  int tx = rx - 6 - TODAY_W;
  win_fill(tx, PAD, TODAY_W, 24, C_BUTTON);
  center(tx, TODAY_W, PAD + 4, "Today", C_TEXT);
  e = cat(s, months[mon]);
  *e++ = ' ';
  num(e, year);
  center(PAD + BTN_W, tx - PAD - BTN_W, PAD + 4, s, C_HEAD);

  int gx = PAD, gy = HEAD_H;
  for(int i = 0; i < 7; i++)
    center(gx + i * CELL_W, CELL_W, gy + 4, wdays[i], i == 0 || i == 6 ? C_WEEKND : C_DIM);
  gy += WDAY_H;

  // six weeks from the Sunday on or before the 1st
  int first = wday(year, mon, 1);
  int py = mon ? year : year - 1, pm = mon ? mon - 1 : 11;
  for(int cell = 0; cell < 42; cell++){
    int y = year, m = mon, d = cell - first + 1;
    if(d < 1){
      y = py;
      m = pm;
      d += mdays(py, pm);
    } else if(d > mdays(year, mon)){
      d -= mdays(year, mon);
      m = mon == 11 ? 0 : mon + 1;
      y = mon == 11 ? year + 1 : year;
    }
    int cx = gx + cell % 7 * CELL_W, cy = gy + cell / 7 * CELL_H;
    int istoday = y == now.tm_year + 1900 && m == now.tm_mon && d == now.tm_mday;
    uint32 c = m != mon ? C_DIM : cell % 7 == 0 || cell % 7 == 6 ? C_WEEKND : C_TEXT;
    if(seld && y == sely && m == selm && d == seld)
      win_fill(cx + 2, cy + 2, CELL_W - 4, CELL_H - 4, C_SEL);
    if(istoday){
      win_fill(cx + 6, cy + 4, CELL_W - 12, CELL_H - 8, C_TODAY);
      c = 0xffffff;
    }
    num(s, d);
    center(cx, CELL_W, cy + (CELL_H - FONT_H) / 2, s, c);
  }

  // the selected day, or now
  int fy = win_h - FOOT_H;
  win_fill(0, fy, win_w, FOOT_H, C_FOOT);
  if(seld){
    e = cat(s, wdaynames[wday(sely, selm, seld)]);
    e = cat(e, ", ");
    e = num(e, seld);
    *e++ = ' ';
    e = cat(e, months[selm]);
    *e++ = ' ';
    num(e, sely);
  } else {
    e = cat(s, wdaynames[now.tm_wday]);
    e = cat(e, ", ");
    e = num(e, now.tm_mday);
    *e++ = ' ';
    e = cat(e, months[now.tm_mon]);
    *e++ = ' ';
    e = num(e, now.tm_year + 1900);
    e = cat(e, ", ");
    *e++ = '0' + now.tm_hour / 10;
    *e++ = '0' + now.tm_hour % 10;
    *e++ = ':';
    *e++ = '0' + now.tm_min / 10;
    *e++ = '0' + now.tm_min % 10;
    cat(e, " UTC");
  }
  win_text(PAD, fy + 4, s, C_TEXT);
  win_update(0, 0, win_w, win_h);
}

static void
month(int n)
{
  int m = year * 12 + mon + n;
  if(m < 1970 * 12)
    m = 1970 * 12;
  year = m / 12;
  mon = m % 12;
}

static void
today(void)
{
  year = now.tm_year + 1900;
  mon = now.tm_mon;
  seld = 0;
}

static void
click(int x, int y)
{
  int rx = win_w - PAD - BTN_W, tx = rx - 6 - TODAY_W;
  if(y >= PAD && y < PAD + 24){
    if(x >= PAD && x < PAD + BTN_W)
      month(-1);
    else if(x >= rx && x < rx + BTN_W)
      month(1);
    else if(x >= tx && x < tx + TODAY_W)
      today();
    return;
  }
  int gy = HEAD_H + WDAY_H;
  if(y < gy || y >= gy + 6 * CELL_H || x < PAD || x >= PAD + 7 * CELL_W)
    return;
  int cell = (y - gy) / CELL_H * 7 + (x - PAD) / CELL_W;
  int d = cell - wday(year, mon, 1) + 1;
  if(d < 1 || d > mdays(year, mon))
    return;
  if(seld == d && selm == mon && sely == year)
    seld = 0;   // again: unselect
  else {
    sely = year;
    selm = mon;
    seld = d;
  }
}

int
main(void)
{
  int w, h;
  win_screen(&w, &h);
  gmtime(time(), &now);
  today();
  if(win_open(7 * CELL_W + 2 * PAD, HEAD_H + WDAY_H + 6 * CELL_H + PAD + FOOT_H, "Calendar") < 0)
    exit(1);
  draw();

  // wait for events, and look at the clock every second
  for(;;){
    struct winmsg m;
    int r, changed = 0;
    while((r = win_event(&m, 0)) > 0){
      if(m.type == WE_CLOSE)
        exit(0);
      if(m.type == WE_MOUSE && m.b == MOUSE_DOWN && m.a == 1){
        click(m.x, m.y);
        changed = 1;
      } else if(m.type == WE_MOUSE && m.b == MOUSE_WHEEL){
        month(-m.a);
        changed = 1;
      } else if(m.type == WE_KEY){
        switch(m.a){
        case KEY_LEFT: case KEY_UP: case KEY_PGUP: month(-1); break;
        case KEY_RIGHT: case KEY_DOWN: case KEY_PGDN: month(1); break;
        case KEY_HOME: case 't': today(); break;
        case 'q': exit(0);
        }
        changed = 1;
      }
    }
    if(r < 0)
      exit(0);
    struct tm t;
    gmtime(time(), &t);
    if(t.tm_min != now.tm_min || t.tm_mday != now.tm_mday){
      now = t;
      changed = 1;
    }
    if(changed)
      draw();
    pause(10);
  }
}
