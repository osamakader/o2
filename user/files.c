// files [dir]: a file explorer on the desktop. Lists a directory,
// folders first; click to select, double-click (or Enter) to open: a
// folder goes in, a text file opens in view, a program runs (desktop
// apps by themselves, others in a terminal). Up, or Backspace, goes to
// the parent folder; the wheel, arrows and Page Up/Down scroll.

#include "types.h"
#include "user.h"
#include "fcntl.h"
#include "stat.h"
#include "param.h"
#include "fs.h"
#include "win.h"

#define NENT    256
#define ROW_H   20
#define BAR_H   26    // the path bar at the top
#define STATUS_H  20    // the status line at the bottom
#define UP_W    40    // the Up button
#define DCLICK  50    // ticks between the clicks of a double-click

#define C_BG     0xf4f4f2
#define C_BAR    0xe0e2e6
#define C_TEXT   0x202428
#define C_DIM    0x70767c
#define C_SEL    0x2f6fb8
#define C_SELTXT 0xffffff
#define C_BUTTON 0xc8ccd2

enum { K_DIR, K_PROG, K_TEXT, K_DEV, K_LINK };

static struct ent {
  char name[DIRSIZ + 1];
  int kind;
  uint64 size;
} ents[NENT];
static int nent;
static char cwd[MAXPATH] = "/";
static int sel = -1;          // selected entry
static int scroll;            // the first entry shown
static int rows;              // entries that fit
static int lastclick = -100, lastsel = -1;

// desktop apps run by themselves; other programs in a terminal
static char *apps[] = { "term", "files", "view" };

static void
join(char *buf, char *dir, char *name)
{
  strcpy(buf, dir);
  if(buf[strlen(buf) - 1] != '/')
    strcpy(buf + strlen(buf), "/");
  strcpy(buf + strlen(buf), name);
}

// What path is: a program (ELF, or a #! script), or text.
static int
kindof(char *path)
{
  char b[4];
  int fd = open(path, O_RDONLY);
  if(fd < 0)
    return K_TEXT;
  int n = read(fd, b, 4);
  close(fd);
  if(n == 4 && b[0] == 0x7f && b[1] == 'E' && b[2] == 'L' && b[3] == 'F')
    return K_PROG;
  if(n >= 2 && b[0] == '#' && b[1] == '!')
    return K_PROG;
  return K_TEXT;
}

static int
before(struct ent *a, struct ent *b)
{
  if((a->kind == K_DIR) != (b->kind == K_DIR))
    return a->kind == K_DIR;
  return strcmp(a->name, b->name) < 0;
}

static void
load(void)
{
  nent = 0;
  sel = -1;
  scroll = 0;
  int fd = open(cwd, O_RDONLY);
  if(fd < 0)
    return;
  struct dirent de;
  char path[MAXPATH + DIRSIZ + 2];
  while(read(fd, &de, sizeof(de)) == sizeof(de) && nent < NENT){
    if(de.inum == 0)
      continue;
    struct ent *e = &ents[nent];
    memmove(e->name, de.name, DIRSIZ);
    e->name[DIRSIZ] = 0;
    if(strcmp(e->name, ".") == 0 || strcmp(e->name, "..") == 0)
      continue;
    join(path, cwd, e->name);
    struct stat st;
    if(lstat(path, &st) < 0)
      continue;
    e->size = st.size;
    e->kind = st.type == T_DIR ? K_DIR : st.type == T_DEVICE ? K_DEV :
              st.type == T_SYMLINK ? K_LINK : kindof(path);
    if(e->kind == K_LINK && stat(path, &st) == 0)
      e->kind = st.type == T_DIR ? K_DIR : kindof(path);
    // insertion sort: folders first, then by name
    struct ent t = *e;
    int i;
    for(i = nent; i > 0 && before(&t, &ents[i - 1]); i--)
      ents[i] = ents[i - 1];
    ents[i] = t;
    nent++;
  }
  close(fd);
  char title[44] = "Files: ";
  int n = strlen(cwd);
  if(n > 36)
    n = 36;
  memmove(title + 7, cwd + strlen(cwd) - n, n);
  title[7 + n] = 0;
  win_title(title);
}

// a 16x16 icon at (x, y)
static void
icon(int x, int y, int kind)
{
  switch(kind){
  case K_DIR:
    win_fill(x + 1, y + 2, 7, 3, 0xd9a520);
    win_fill(x + 1, y + 4, 14, 10, 0xf0c040);
    break;
  case K_DEV:
    win_fill(x + 2, y + 3, 12, 10, 0x707880);
    win_fill(x + 4, y + 5, 8, 6, 0xa0a8b0);
    break;
  default:
    // a page; programs get a green band
    win_fill(x + 3, y + 1, 10, 14, 0x8890a0);
    win_fill(x + 4, y + 2, 8, 12, 0xffffff);
    if(kind == K_PROG)
      win_fill(x + 4, y + 5, 8, 4, 0x40b060);
    else
      for(int j = 4; j < 13; j += 3)
        win_fill(x + 5, y + j, 6, 1, 0x9098a8);
  }
}

static void
draw(void)
{
  win_fill(0, 0, win_w, win_h, C_BG);
  // the path bar
  win_fill(0, 0, win_w, BAR_H, C_BAR);
  win_fill(4, 4, UP_W, BAR_H - 8, C_BUTTON);
  win_text(4 + (UP_W - 2 * FONT_W) / 2, 5, "Up", C_TEXT);
  win_text(UP_W + 12, 5, cwd, C_TEXT);

  for(int i = scroll; i < nent && i < scroll + rows; i++){
    struct ent *e = &ents[i];
    int y = BAR_H + (i - scroll) * ROW_H;
    uint32 fg = C_TEXT;
    if(i == sel){
      win_fill(0, y, win_w, ROW_H, C_SEL);
      fg = C_SELTXT;
    }
    icon(8, y + 2, e->kind);
    win_text(32, y + 2, e->name, fg);
    if(e->kind != K_DIR && e->kind != K_DEV){
      char s[24];
      int n = 0;
      uint64 v = e->size;
      char *unit = "";
      if(v >= 10240){
        v /= 1024;
        unit = "K";
      }
      char d[20];
      int nd = 0;
      do
        d[nd++] = '0' + v % 10;
      while((v /= 10) > 0);
      while(nd > 0)
        s[n++] = d[--nd];
      strcpy(s + n, unit);
      win_text(win_w - 8 - strlen(s) * FONT_W, y + 2, s, i == sel ? fg : C_DIM);
    }
  }

  // the status line
  int y = win_h - STATUS_H;
  win_fill(0, y, win_w, STATUS_H, C_BAR);
  char s[48];
  int n = 0, v = nent;
  char d[8];
  int nd = 0;
  do
    d[nd++] = '0' + v % 10;
  while((v /= 10) > 0);
  while(nd > 0)
    s[n++] = d[--nd];
  strcpy(s + n, nent == 1 ? " item" : " items");
  win_text(8, y + 2, s, C_DIM);
  win_update(0, 0, win_w, win_h);
}

static void
up(void)
{
  int n = strlen(cwd);
  if(n <= 1)
    return;
  while(n > 1 && cwd[n - 1] != '/')
    n--;
  cwd[n > 1 ? n - 1 : 1] = 0;
  load();
}

static void
open_(int i)
{
  if(i < 0 || i >= nent)
    return;
  struct ent *e = &ents[i];
  char path[MAXPATH + DIRSIZ + 2], cmd[MAXPATH + DIRSIZ + 8];
  join(path, cwd, e->name);
  if(e->kind == K_DIR){
    if(strlen(path) < MAXPATH){
      strcpy(cwd, path);
      load();
    }
    return;
  }
  if(e->kind == K_DEV)
    return;
  if(e->kind == K_PROG){
    int app = 0;
    for(int k = 0; k < sizeof(apps) / sizeof(apps[0]); k++)
      if(strcmp(cwd, "/") == 0 && strcmp(e->name, apps[k]) == 0)
        app = 1;
    strcpy(cmd, app ? "" : "term ");
  } else
    strcpy(cmd, "view ");
  strcpy(cmd + strlen(cmd), path);
  win_launch(cmd);
}

static void
select(int i)
{
  if(nent == 0)
    return;
  if(i < 0)
    i = 0;
  if(i >= nent)
    i = nent - 1;
  sel = i;
  if(sel < scroll)
    scroll = sel;
  if(sel >= scroll + rows)
    scroll = sel - rows + 1;
}

static void
scrollby(int n)
{
  scroll += n;
  if(scroll > nent - rows)
    scroll = nent - rows;
  if(scroll < 0)
    scroll = 0;
}

static void
click(int x, int y)
{
  if(y < BAR_H){
    if(x >= 4 && x < 4 + UP_W)
      up();
    return;
  }
  int i = scroll + (y - BAR_H) / ROW_H;
  if(y >= win_h - STATUS_H || i >= nent){
    sel = -1;
    return;
  }
  int now = uptime();
  if(i == lastsel && now - lastclick < DCLICK){
    lastclick = -100;
    open_(i);
    return;
  }
  sel = i;
  lastsel = i;
  lastclick = now;
}

int
main(int argc, char *argv[])
{
  int w, h;
  win_screen(&w, &h);
  if(w > 480)
    w = 480;
  if(h > 400)
    h = 400;
  if(argc > 1 && strlen(argv[1]) < MAXPATH)
    strcpy(cwd, argv[1]);
  if(win_open(w, h, "Files") < 0)
    exit(1);
  rows = (win_h - BAR_H - STATUS_H) / ROW_H;
  load();
  draw();

  struct winmsg m;
  int r;
  while((r = win_event(&m, 1)) >= 0){
    if(r == 0)
      continue;
    switch(m.type){
    case WE_CLOSE:
      exit(0);
    case WE_MOUSE:
      if(m.b == MOUSE_DOWN && m.a == 1)
        click(m.x, m.y);
      else if(m.b == MOUSE_WHEEL)
        scrollby(-3 * m.a);
      else
        continue;
      break;
    case WE_KEY:
      switch(m.a){
      case KEY_UP: select(sel - 1); break;
      case KEY_DOWN: select(sel + 1); break;
      case KEY_PGUP: select(sel - rows); break;
      case KEY_PGDN: select(sel + rows); break;
      case KEY_HOME: select(0); break;
      case KEY_END: select(nent - 1); break;
      case '\n': case '\r': open_(sel); break;
      case '\b': case 127: up(); break;
      default: continue;
      }
      break;
    default:
      continue;
    }
    draw();
  }
  exit(0);
}
