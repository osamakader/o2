// edit [file]: a text editor on the desktop. Type to insert; arrows,
// Home/End, Page Up/Down and a click move; Backspace and Delete delete.
//   ^S save (asks for a name if there is none)   ^Q quit
//   ^F find (Enter again: the next)              ^G go to a line
//   ^K cut the line                              ^U paste it back
// The text is a gap buffer: an array with the gap at the last edit.

#include "types.h"
#include "user.h"
#include "fcntl.h"
#include "stat.h"
#include "win.h"

#define COLS     80
#define ROWS     30
#define PAD      4
#define STATUS_H 20
#define C(x)     ((x) & 0x1f)

#define C_BG     0xfbfbf8
#define C_TEXT   0x202428
#define C_CURSOR 0x2f6fb8
#define C_BAR    0xe0e2e6
#define C_DIM    0x70767c
#define C_PROMPT 0xfff3c4

// the text: buf[0, gs) and buf[ge, cap), the gap between
static char *buf;
static int cap, gs, ge;

static int cur;           // the cursor, an offset in the text
static int top;           // the first line shown (its offset)
static int left;          // the first column shown
static int want = -1;     // the column up and down keep to
static int rows, cols;
static char name[64];
static int modified, quitting;
static char msg[64];      // in the status bar until the next key
static char clip[1024];   // ^K's line
static int cliplen;

// a question in the status bar: what to do with the answer
static char *prompt;
static char answer[64];
static int alen;
static void (*answered)(void);
static char found[64];    // what ^F looks for

static int
size(void)
{
  return cap - (ge - gs);
}

static int
at(int i)
{
  return i < gs ? buf[i] : buf[i + ge - gs];
}

// Move the gap to i.
static void
gapto(int i)
{
  if(i < gs)
    memmove(buf + ge - (gs - i), buf + i, gs - i);
  else if(i > gs)
    memmove(buf + gs, buf + ge, i - gs);
  ge += i - gs;
  gs = i;
}

static void
insert(int c)
{
  if(gs == ge){
    // full: twice as big
    int ncap = cap * 2, tail = cap - ge;
    char *nb = malloc(ncap);
    memmove(nb, buf, gs);
    memmove(nb + ncap - tail, buf + ge, tail);
    free(buf);
    buf = nb;
    ge = ncap - tail;
    cap = ncap;
  }
  gapto(cur);
  buf[gs++] = c;
  cur++;
  modified = 1;
}

// Delete the character at i.
static void
delete(int i)
{
  if(i < 0 || i >= size())
    return;
  gapto(i);
  ge++;
  if(cur > i)
    cur--;
  modified = 1;
}

static int
linestart(int i)
{
  while(i > 0 && at(i - 1) != '\n')
    i--;
  return i;
}

static int
lineend(int i)
{
  while(i < size() && at(i) != '\n')
    i++;
  return i;
}

// the column (tabs to multiples of 8) of offset i
static int
column(int i)
{
  int c = 0;
  for(int j = linestart(i); j < i; j++)
    c = at(j) == '\t' ? (c + 8) & ~7 : c + 1;
  return c;
}

// The offset in the line starting at s nearest column col.
static int
atcolumn(int s, int col)
{
  int c = 0, i = s;
  while(i < size() && at(i) != '\n'){
    int n = at(i) == '\t' ? (c + 8) & ~7 : c + 1;
    if(n > col)
      break;
    c = n;
    i++;
  }
  return i;
}

// the line number (from 1) of offset i
static int
lineof(int i)
{
  int n = 1;
  for(int j = 0; j < i; j++)
    if(at(j) == '\n')
      n++;
  return n;
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

static void
settitle(void)
{
  char t[44] = "edit: ";
  char *base = name[0] ? name + strlen(name) : "untitled";
  if(name[0])
    while(base > name && base[-1] != '/')
      base--;
  int n = strlen(base) < 35 ? strlen(base) : 35;
  memmove(t + 6, base, n);
  if(modified)
    t[6 + n] = '*';
  win_title(t);
}

// Keep the cursor on the screen.
static void
follow(void)
{
  if(cur < top)
    top = linestart(cur);
  int n = 0;
  for(int i = top; i < cur; i++)
    if(at(i) == '\n')
      n++;
  for(; n >= rows; n--)
    top = lineend(top) + 1;
  int c = column(cur);
  if(c < left)
    left = c;
  if(c >= left + cols)
    left = c - cols + 1;
}

static void
draw(void)
{
  follow();
  win_fill(0, 0, win_w, win_h, C_BG);
  int i = top;
  for(int r = 0; r < rows; r++){
    int c = 0;
    for(;; i++){
      if(i == cur && c >= left && c < left + cols)
        win_fill(PAD + (c - left) * FONT_W, PAD + r * FONT_H, 2, FONT_H, C_CURSOR);
      if(i >= size() || at(i) == '\n')
        break;
      int ch = at(i);
      if(ch == '\t'){
        c = (c + 8) & ~7;
        continue;
      }
      if(c >= left && c < left + cols)
        win_char(PAD + (c - left) * FONT_W, PAD + r * FONT_H, ch, C_TEXT);
      c++;
    }
    if(i >= size())
      break;
    i++;
  }

  // the status bar: a question, a message, or where we are
  int y = win_h - STATUS_H;
  char s[128], *e;
  if(prompt){
    win_fill(0, y, win_w, STATUS_H, C_PROMPT);
    e = s;
    strcpy(e, prompt);
    e += strlen(e);
    memmove(e, answer, alen);
    e[alen] = 0;
    win_text(PAD, y + 2, s, C_TEXT);
    win_fill(PAD + strlen(s) * FONT_W, y + 2, 2, FONT_H, C_CURSOR);
  } else {
    win_fill(0, y, win_w, STATUS_H, C_BAR);
    if(msg[0]){
      win_text(PAD, y + 2, msg, C_TEXT);
    } else {
      e = s;
      strcpy(e, "line ");
      e = num(e + 5, lineof(cur));
      strcpy(e, ", col ");
      num(e + 6, column(cur) + 1);
      win_text(PAD, y + 2, s, C_DIM);
      char *help = "^S save ^Q quit ^F find ^G line ^K cut ^U paste";
      int x = win_w - PAD - strlen(help) * FONT_W;
      if(x > PAD + 20 * FONT_W)
        win_text(x, y + 2, help, C_DIM);
    }
  }
  win_update(0, 0, win_w, win_h);
}

static int
load(char *path)
{
  int fd = open(path, O_RDONLY);
  struct stat st;
  if(fd < 0)
    return -1;
  if(fstat(fd, &st) < 0 || st.type != T_FILE){
    close(fd);
    return -2;
  }
  cap = st.size + 4096;
  buf = malloc(cap);
  int n = 0, r;
  while(n < st.size && (r = read(fd, buf + n, st.size - n)) > 0)
    n += r;
  close(fd);
  // the text at the start, the gap after it
  memmove(buf + cap - n, buf, n);
  gs = 0;
  ge = cap - n;
  return 0;
}

static int
save(void)
{
  int fd = open(name, O_WRONLY | O_CREATE | O_TRUNC);
  if(fd < 0)
    return -1;
  int ok = write(fd, buf, gs) == gs && write(fd, buf + ge, cap - ge) == cap - ge;
  close(fd);
  if(!ok)
    return -1;
  modified = 0;
  settitle();
  char *e = num(msg, size());
  strcpy(e, " bytes written");
  return 0;
}

static void
ask(char *q, void (*then)(void))
{
  prompt = q;
  alen = 0;
  answered = then;
}

static void
saveas(void)
{
  if(alen == 0)
    return;
  memmove(name, answer, alen);
  name[alen] = 0;
  if(save() < 0)
    strcpy(msg, "cannot write the file");
}

static void
dosave(void)
{
  if(name[0] == 0)
    ask("Save as: ", saveas);
  else if(save() < 0)
    strcpy(msg, "cannot write the file");
}

// Show the cursor's line in the middle (after a jump).
static void
center(void)
{
  top = linestart(cur);
  for(int n = rows / 2; n > 0 && top > 0; n--)
    top = linestart(top - 1);
}

// From after the cursor, around: the next found.
static void
findnext(void)
{
  int n = strlen(found), sz = size();
  if(n == 0)
    return;
  for(int k = 1; k <= sz; k++){
    int i = (cur + k) % sz, j;
    for(j = 0; j < n && i + j < sz && at(i + j) == found[j]; j++)
      ;
    if(j == n){
      cur = i;
      center();
      return;
    }
  }
  strcpy(msg, "not found");
}

static void
find(void)
{
  if(alen > 0){
    memmove(found, answer, alen);
    found[alen] = 0;
  }
  findnext();
}

static void
gotoline(void)
{
  int n = 0;
  for(int i = 0; i < alen; i++)
    if(answer[i] >= '0' && answer[i] <= '9')
      n = n * 10 + answer[i] - '0';
  int i = 0;
  for(int l = 1; l < n && i < size(); i++)
    if(at(i) == '\n')
      l++;
  cur = i;
  center();
}

// A key while a question is asked.
static void
promptkey(int c)
{
  if(c == '\n' || c == '\r'){
    prompt = 0;
    answered();
  } else if(c == '\033' || c == C('C') || c == C('Q')){
    prompt = 0;
  } else if(c == '\b' || c == 127){
    if(alen > 0)
      alen--;
  } else if(c >= ' ' && c < 127 && alen < sizeof(answer) - 1){
    answer[alen++] = c;
  }
}

static void
key(int c)
{
  int wasmod = modified;
  msg[0] = 0;
  if(prompt){
    promptkey(c);
    return;
  }
  if(c != C('Q'))
    quitting = 0;
  if(c != KEY_UP && c != KEY_DOWN && c != KEY_PGUP && c != KEY_PGDN)
    want = -1;
  switch(c){
  case KEY_LEFT:
    if(cur > 0)
      cur--;
    break;
  case KEY_RIGHT:
    if(cur < size())
      cur++;
    break;
  case KEY_UP: case KEY_DOWN: case KEY_PGUP: case KEY_PGDN: {
    if(want < 0)
      want = column(cur);
    int n = c == KEY_UP || c == KEY_DOWN ? 1 : rows - 1;
    int s = linestart(cur);
    for(; n > 0; n--){
      if(c == KEY_UP || c == KEY_PGUP){
        if(s == 0)
          break;
        s = linestart(s - 1);
      } else {
        int e = lineend(s);
        if(e >= size())
          break;
        s = e + 1;
      }
    }
    cur = atcolumn(s, want);
    break;
  }
  case KEY_HOME:
    cur = linestart(cur);
    break;
  case KEY_END:
    cur = lineend(cur);
    break;
  case '\b': case 127:
    if(cur > 0)
      delete(cur - 1);
    break;
  case KEY_DEL:
    delete(cur);
    break;
  case '\r':
    insert('\n');
    break;
  case C('S'):
    dosave();
    break;
  case C('Q'):
    if(modified && !quitting){
      quitting = 1;
      strcpy(msg, "unsaved changes: ^Q again to quit, ^S to save");
    } else
      exit(0);
    break;
  case C('F'):
    ask("Find: ", find);
    break;
  case C('G'):
    ask("Go to line: ", gotoline);
    break;
  case C('K'): {
    // the whole line, with its newline
    int s = linestart(cur), e = lineend(cur);
    if(e < size())
      e++;
    cliplen = e - s < sizeof(clip) ? e - s : sizeof(clip);
    for(int i = 0; i < cliplen; i++)
      clip[i] = at(s + i);
    cur = s;
    for(int i = s; i < e; i++)
      delete(s);
    break;
  }
  case C('U'):
    for(int i = 0; i < cliplen; i++)
      insert(clip[i]);
    break;
  default:
    if((c >= ' ' && c < 127) || c == '\t' || c == '\n')
      insert(c);
  }
  if(modified != wasmod)
    settitle();
}

// A click at (x, y) in the window: the cursor there.
static void
click(int x, int y)
{
  int r = (y - PAD) / FONT_H, s = top;
  if(y >= win_h - STATUS_H)
    return;
  for(; r > 0; r--){
    int e = lineend(s);
    if(e >= size())
      break;
    s = e + 1;
  }
  cur = atcolumn(s, left + (x - PAD + FONT_W / 2) / FONT_W);
  want = -1;
}

int
main(int argc, char *argv[])
{
  if(argc > 1){
    if(strlen(argv[1]) >= sizeof(name)){
      fprintf(2, "edit: name too long\n");
      exit(1);
    }
    strcpy(name, argv[1]);
    int r = load(name);
    if(r == -2){
      fprintf(2, "edit: %s is not a file\n", name);
      exit(1);
    }
    if(r < 0)
      strcpy(msg, "new file");
  }
  if(buf == 0){
    cap = 4096;
    buf = malloc(cap);
    gs = 0;
    ge = cap;
  }

  int w, h;
  win_screen(&w, &h);
  cols = (w - 2 * PAD) / FONT_W;
  if(cols > COLS)
    cols = COLS;
  rows = (h - 2 * PAD - STATUS_H) / FONT_H;
  if(rows > ROWS)
    rows = ROWS;
  if(win_open(cols * FONT_W + 2 * PAD, rows * FONT_H + 2 * PAD + STATUS_H, "edit") < 0)
    exit(1);
  settitle();
  draw();

  struct winmsg m;
  int r;
  while((r = win_event(&m, 1)) >= 0){
    if(r == 0)
      continue;
    if(m.type == WE_CLOSE){
      if(!modified)
        break;
      key(C('Q'));   // ask first; desk's second click closes anyway
    } else if(m.type == WE_KEY){
      key(m.a);
    } else if(m.type == WE_MOUSE && m.b == MOUSE_DOWN && m.a == 1){
      click(m.x, m.y);
    } else if(m.type == WE_MOUSE && m.b == MOUSE_WHEEL){
      for(int n = 3 * m.a; n > 0 && top > 0; n--)
        top = linestart(top - 1);
      for(int n = -3 * m.a; n > 0 && lineend(top) < size(); n--)
        top = lineend(top) + 1;
      // the cursor stays where it is shown
      int lines = 0;
      for(int i = top; i < cur && lines < rows; i++)
        if(at(i) == '\n')
          lines++;
      if(cur < top)
        cur = top;
      else if(lines >= rows){
        int s = top;
        for(int n = rows - 1; n > 0; n--)
          s = lineend(s) + 1;
        cur = s;
      }
    } else
      continue;
    draw();
  }
  exit(0);
}
