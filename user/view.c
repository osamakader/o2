// view file: a text file in a window on the desktop. The wheel, arrows,
// Page Up/Down, Home and End scroll; q closes it.

#include "types.h"
#include "user.h"
#include "fcntl.h"
#include "stat.h"
#include "win.h"

#define MAXSIZE (256 * 1024)
#define MAXLINES 8192
#define COLS    80
#define ROWS    30
#define PAD     4
#define STATUS_H  20

#define C_BG   0xfbfbf8
#define C_TEXT 0x202428
#define C_BAR  0xe0e2e6
#define C_DIM  0x70767c

static char *data;
static int lines[MAXLINES], nlines;   // where each line starts
static int top;                       // the first line shown
static int rows, cols;

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

static void
draw(void)
{
  win_fill(0, 0, win_w, win_h, C_BG);
  for(int r = 0; r < rows && top + r < nlines; r++){
    char *p = data + lines[top + r];
    int c = 0;
    for(; *p && *p != '\n' && c < cols; p++){
      if(*p == '\t'){
        c = (c + 8) & ~7;
        continue;
      }
      win_char(PAD + c * FONT_W, PAD + r * FONT_H, *p, C_TEXT);
      c++;
    }
  }
  int y = win_h - STATUS_H;
  win_fill(0, y, win_w, STATUS_H, C_BAR);
  char s[48], *e = s;
  e = num(e, nlines ? top + 1 : 0);
  strcpy(e, "-");
  e = num(e + 1, top + rows < nlines ? top + rows : nlines);
  strcpy(e, " of ");
  e = num(e + 4, nlines);
  strcpy(e, " lines");
  win_text(PAD, y + 2, s, C_DIM);
  win_update(0, 0, win_w, win_h);
}

static void
scrollto(int t)
{
  if(t > nlines - rows)
    t = nlines - rows;
  if(t < 0)
    t = 0;
  top = t;
}

int
main(int argc, char *argv[])
{
  if(argc != 2){
    fprintf(2, "usage: view file\n");
    exit(1);
  }
  int fd = open(argv[1], O_RDONLY);
  struct stat st;
  if(fd < 0 || fstat(fd, &st) < 0 || st.type != T_FILE){
    fprintf(2, "view: cannot open %s\n", argv[1]);
    exit(1);
  }
  int size = st.size < MAXSIZE ? st.size : MAXSIZE;
  data = malloc(size + 1);
  int n = 0, r;
  while(n < size && (r = read(fd, data + n, size - n)) > 0)
    n += r;
  data[n] = 0;
  close(fd);
  for(int i = 0; i < n && nlines < MAXLINES; ){
    lines[nlines++] = i;
    while(i < n && data[i] != '\n')
      i++;
    i++;
  }

  int w, h;
  win_screen(&w, &h);
  cols = (w - 2 * PAD) / FONT_W;
  if(cols > COLS)
    cols = COLS;
  rows = (h - 2 * PAD - STATUS_H) / FONT_H;
  if(rows > ROWS)
    rows = ROWS;
  char title[44] = "view: ";
  char *name = argv[1] + strlen(argv[1]);
  while(name > argv[1] && name[-1] != '/')
    name--;
  memmove(title + 6, name, strlen(name) < 37 ? strlen(name) : 37);
  if(win_open(cols * FONT_W + 2 * PAD, rows * FONT_H + 2 * PAD + STATUS_H, title) < 0)
    exit(1);
  draw();

  struct winmsg m;
  while((r = win_event(&m, 1)) >= 0){
    if(r == 0)
      continue;
    if(m.type == WE_CLOSE)
      break;
    int old = top, t = top;
    if(m.type == WE_MOUSE && m.b == MOUSE_WHEEL)
      t -= 3 * m.a;
    else if(m.type == WE_KEY){
      switch(m.a){
      case KEY_UP: t--; break;
      case KEY_DOWN: case '\n': case '\r': t++; break;
      case KEY_PGUP: t -= rows - 1; break;
      case KEY_PGDN: case ' ': t += rows - 1; break;
      case KEY_HOME: t = 0; break;
      case KEY_END: t = nlines; break;
      case 'q': exit(0);
      }
    }
    scrollto(t);
    if(top != old)
      draw();
  }
  exit(0);
}
