// term [command]: a terminal window on the desktop, running a shell
// through pipes; with a command, the shell runs it first. The terminal
// echoes and edits the line itself and hands it to the shell with
// Enter: ^U kills the line, ^C interrupts what the shell runs, ^D on
// an empty line ends the shell, and with it the window.

#include "types.h"
#include "user.h"
#include "fcntl.h"
#include "procinfo.h"
#include "win.h"

#define COLS 80
#define ROWS 24
#define PAD  4

#define C_BG 0x101214
#define C_FG 0xd8d8d8

static char text[ROWS][COLS];
static int cols, rows;
static int cx, cy;          // the cursor
static int esc;             // in an escape sequence the shell printed
static char line[128];      // being typed
static int len;
static int focused = 1;
static int dirty0 = ROWS, dirty1 = -1;   // rows to draw again
static int pid, in, out;    // the shell, its stdin and stdout

static void
dirty(int r)
{
  if(r < dirty0)
    dirty0 = r;
  if(r > dirty1)
    dirty1 = r;
}

static void
draw(void)
{
  if(dirty1 < dirty0)
    return;
  int y0 = PAD + dirty0 * FONT_H, h = (dirty1 - dirty0 + 1) * FONT_H;
  win_fill(0, y0, win_w, h, C_BG);
  for(int r = dirty0; r <= dirty1; r++){
    for(int c = 0; c < cols; c++)
      win_char(PAD + c * FONT_W, PAD + r * FONT_H, text[r][c], C_FG);
    if(r == cy && focused){
      win_fill(PAD + cx * FONT_W, PAD + r * FONT_H, FONT_W, FONT_H, C_FG);
      win_char(PAD + cx * FONT_W, PAD + r * FONT_H, cx < cols ? text[r][cx] : 0, C_BG);
    }
  }
  win_update(0, y0, win_w, h);
  dirty0 = ROWS;
  dirty1 = -1;
}

static void
newline(void)
{
  dirty(cy);
  cx = 0;
  if(++cy < rows)
    return;
  memmove(text[0], text[1], sizeof(text[0]) * (rows - 1));
  memset(text[rows - 1], 0, sizeof(text[0]));
  cy = rows - 1;
  dirty(0);
  dirty(rows - 1);
}

// Put a character the shell printed, or one typed, on the screen.
static void
put(int c)
{
  dirty(cy);
  // escape sequences: ESC [ ... letter; dropped
  if(esc){
    if(esc == 1 && c != '[')
      esc = 0;
    else if(esc == 2 && c >= '@' && c <= '~')
      esc = 0;
    else
      esc = 2;
    return;
  }
  switch(c){
  case '\033':
    esc = 1;
    break;
  case '\n':
    newline();
    break;
  case '\r':
    cx = 0;
    break;
  case '\b':
    if(cx > 0)
      text[cy][--cx] = 0;
    break;
  case '\t':
    do
      put(' ');
    while(cx % 8);
    break;
  default:
    if(c < ' ' || c >= 127)
      break;
    if(cx == cols)
      newline();
    text[cy][cx++] = c;
  }
  dirty(cy);
}

// SIGINT to what the shell runs, and the shell (which prompts again)
static void
interrupt(void)
{
  static struct procinfo ps[64];
  int n = getprocs(ps, 64);
  for(int i = 0; i < n; i++){
    // is the shell an ancestor (or itself)?
    int p = ps[i].pid;
    for(int hops = 0; p && p != pid && hops < n; hops++){
      int j;
      for(j = 0; j < n && ps[j].pid != p; j++)
        ;
      p = j < n ? ps[j].ppid : 0;
    }
    if(p == pid && ps[i].state != PS_ZOMBIE)
      kill(ps[i].pid, SIGINT);
  }
}

static void
key(int c)
{
  if(c == ('C' & 0x1f)){
    put('^');
    put('C');
    put('\n');
    len = 0;
    interrupt();
  } else if(c == ('D' & 0x1f)){
    if(len == 0 && in >= 0){
      close(in);   // the shell reads its end, and exits
      in = -1;
    }
  } else if(c == ('U' & 0x1f)){
    for(; len > 0; len--)
      put('\b');
  } else if(c == '\b' || c == 127){
    if(len > 0){
      len--;
      put('\b');
    }
  } else if(c == '\n' || c == '\r'){
    put('\n');
    line[len++] = '\n';
    if(in >= 0)
      write(in, line, len);
    len = 0;
  } else if((c >= ' ' && c < 127) || c == '\t'){
    if(len < sizeof(line) - 1){
      line[len++] = c;
      put(c);
    }
  }
}

static void
shell(void)
{
  int toin[2], fromout[2];
  if(pipe(toin) < 0 || pipe(fromout) < 0)
    exit(1);
  pid = fork();
  if(pid < 0)
    exit(1);
  if(pid == 0){
    dup2(toin[0], 0);
    dup2(fromout[1], 1);
    dup2(fromout[1], 2);
    // nothing of the desktop's: it waits for our pipes to end
    for(int fd = 3; fd < 32; fd++)
      close(fd);
    char *argv[] = { "sh", 0 };
    exec("/sh", argv);
    exit(1);
  }
  close(toin[0]);
  close(fromout[1]);
  in = toin[1];
  out = fromout[0];
  fcntl(out, F_SETFL, O_NONBLOCK);
}

int
main(int argc, char *argv[])
{
  int w, h;
  win_screen(&w, &h);
  cols = (w - 2 * PAD) / FONT_W;
  if(cols > COLS)
    cols = COLS;
  rows = (h - 2 * PAD) / FONT_H;
  if(rows > ROWS)
    rows = ROWS;
  // titled after the command, if any
  char title[44] = "terminal";
  if(argc > 1){
    strcpy(title + 8, ": ");
    int n = strlen(argv[1]);
    memmove(title + 10, argv[1], n < 33 ? n : 33);
  }
  if(win_open(cols * FONT_W + 2 * PAD, rows * FONT_H + 2 * PAD, title) < 0)
    exit(1);
  win_fill(0, 0, win_w, win_h, C_BG);
  win_update(0, 0, win_w, win_h);
  // the shell's ^C is for it, and its pipe may close
  signal(SIGINT, SIG_IGN);
  signal(SIGPIPE, SIG_IGN);
  shell();

  // a command to start with, typed for the user at the first prompt
  int typecmd = argc > 1;

  for(;;){
    int busy = 0;
    struct winmsg m;
    int r;
    while((r = win_event(&m, 0)) > 0){
      busy = 1;
      if(m.type == WE_KEY)
        key(m.a);
      else if(m.type == WE_FOCUS){
        focused = m.a;
        dirty(cy);
      } else if(m.type == WE_CLOSE)
        goto done;
    }
    if(r < 0)
      goto done;   // desk is gone

    char buf[256];
    int n = read(out, buf, sizeof(buf));
    if(n == 0)
      goto done;   // the shell exited
    for(int i = 0; i < n; i++)
      put(buf[i]);
    busy |= n > 0;
    if(n > 0 && typecmd){
      typecmd = 0;
      for(int i = 1; i < argc; i++){
        for(char *s = argv[i]; *s; s++)
          key(*s);
        key(i < argc - 1 ? ' ' : '\n');
      }
    }

    draw();
    if(!busy)
      pause(1);
  }

done:
  kill(pid, SIGKILL);
  wait(0);
  exit(0);
}
