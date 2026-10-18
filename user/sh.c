// Shell, from xv6.

#include "types.h"
#include "user.h"
#include "fcntl.h"
#include "syscall.h"
#include "stat.h"
#include "fs.h"

// Parsed command representation
#define EXEC  1
#define REDIR 2
#define PIPE  3
#define LIST  4
#define BACK  5

#define MAXARGS 10

#define C(x)  ((x) - '@')  // control-x

struct cmd {
  int type;
};

struct execcmd {
  int type;
  char *argv[MAXARGS];
  char *eargv[MAXARGS];
};

struct redircmd {
  int type;
  struct cmd *cmd;
  char *file;
  char *efile;
  int mode;
  int fd;
};

struct pipecmd {
  int type;
  struct cmd *left;
  struct cmd *right;
};

struct listcmd {
  int type;
  struct cmd *left;
  struct cmd *right;
};

struct backcmd {
  int type;
  struct cmd *cmd;
};

// Where commands without a "/" are looked for: directories separated
// by ":", an empty one meaning the current directory. Set with
// PATH=dir:dir.
char path[100] = "/";

int fork1(void);  // Fork but panics on failure.
void panic(char*) __attribute__((noreturn));
struct cmd *parsecmd(char*);
void runcmd(struct cmd*) __attribute__((noreturn));

// Try exec'ing name in each directory of path; returns if none worked.
void
execpath(char *name, char **argv)
{
  char file[128];

  for(char *dir = path; ; dir++){
    char *end = strchr(dir, ':');
    int n = end ? end - dir : strlen(dir);
    if(n == 0)
      exec(name, argv);
    else if(n + 1 + strlen(name) + 1 <= sizeof(file)){
      memmove(file, dir, n);
      if(file[n-1] != '/')
        file[n++] = '/';
      strcpy(file + n, name);
      exec(file, argv);
    }
    if(end == 0)
      return;
    dir = end;
  }
}

// Execute cmd.  Never returns.
void
runcmd(struct cmd *cmd)
{
  int p[2];
  struct backcmd *bcmd;
  struct execcmd *ecmd;
  struct listcmd *lcmd;
  struct pipecmd *pcmd;
  struct redircmd *rcmd;

  if(cmd == 0)
    exit(1);

  switch(cmd->type){
  default:
    panic("runcmd");

  case EXEC:
    ecmd = (struct execcmd*)cmd;
    if(ecmd->argv[0] == 0)
      exit(1);
    if(strchr(ecmd->argv[0], '/'))
      exec(ecmd->argv[0], ecmd->argv);
    else
      execpath(ecmd->argv[0], ecmd->argv);
    fprintf(2, "exec %s failed\n", ecmd->argv[0]);
    break;

  case REDIR:
    rcmd = (struct redircmd*)cmd;
    close(rcmd->fd);
    if(open(rcmd->file, rcmd->mode) < 0){
      fprintf(2, "open %s failed\n", rcmd->file);
      exit(1);
    }
    runcmd(rcmd->cmd);
    break;

  case LIST:
    lcmd = (struct listcmd*)cmd;
    if(fork1() == 0)
      runcmd(lcmd->left);
    wait(0);
    runcmd(lcmd->right);
    break;

  case PIPE:
    pcmd = (struct pipecmd*)cmd;
    if(pipe(p) < 0)
      panic("pipe");
    if(fork1() == 0){
      close(1);
      dup(p[1]);
      close(p[0]);
      close(p[1]);
      runcmd(pcmd->left);
    }
    if(fork1() == 0){
      close(0);
      dup(p[0]);
      close(p[0]);
      close(p[1]);
      runcmd(pcmd->right);
    }
    close(p[0]);
    close(p[1]);
    wait(0);
    wait(0);
    break;

  case BACK:
    bcmd = (struct backcmd*)cmd;
    if(fork1() == 0){
      // ^C is for the command in the foreground
      signal(SIGINT, SIG_IGN);
      runcmd(bcmd->cmd);
    }
    break;
  }
  exit(0);
}

int editline(char *buf, int nbuf, int echoed);

// Read a command line from fd into buf, prompting if interactive, and
// drop a "#" comment from it. Returns -1 at end of input. An
// interactive shell on the console edits the line itself (editline()),
// and starts over when ^C interrupts the read.
int
getcmd(int fd, int interactive, char *buf, int nbuf)
{
  int i, r, echoed;
  char c;

again:
  if(interactive)
    write(2, "$ ", 2);
  memset(buf, 0, nbuf);
  if(interactive && (echoed = ioctl(fd, CONSOLE_RAW, 1)) >= 0){
    r = editline(buf, nbuf, echoed);
    ioctl(fd, CONSOLE_RAW, 0);
    if(r == -2)
      goto again;
    if(r < 0)
      return -1;
  } else {
    i = r = 0;
    while(i + 1 < nbuf && (r = read(fd, &c, 1)) == 1){
      buf[i++] = c;
      if(c == '\n' || c == '\r')
        break;
    }
    if(r < 0 && interactive)
      goto again;
    if(buf[0] == 0) // EOF
      return -1;
  }
  for(i = 0; buf[i]; i++){
    if(buf[i] == '#' && (i == 0 || strchr(" \t", buf[i-1]))){
      buf[i] = '\n';
      buf[i+1] = 0;
      break;
    }
  }
  return 0;
}

void
interrupt(int sig)
{
}

// The last NHIST command lines; the n-th line entered (from 1) is
// hist[(n-1) % NHIST].
#define NHIST 20
char hist[NHIST][100];
int nhist;

// "!!" and "!n" stand for the last and the n-th command line: replace
// buf with it. Returns -1 if there's no such line.
int
histexpand(char *buf)
{
  int n;
  char *s;

  if(buf[0] != '!')
    return 0;
  if(buf[1] == '!'){
    n = nhist;
    s = buf + 2;
  } else if(buf[1] >= '0' && buf[1] <= '9'){
    n = atoi(buf + 1);
    for(s = buf + 1; *s >= '0' && *s <= '9'; s++)
      ;
  } else {
    return 0;
  }
  if(*s != '\n' && *s != '\r' && *s != 0)
    return 0;   // not just !! or !n: leave the line alone
  if(n < 1 || n > nhist || n <= nhist - NHIST){
    fprintf(2, "sh: no such command in history\n");
    return -1;
  }
  strcpy(buf, hist[(n-1) % NHIST]);
  fprintf(2, "%s", buf);  // show what runs, like other shells
  return 0;
}

// Line editor, for an interactive shell with the console in raw mode.
// Keys: left/right, home/end (also ^A/^E), backspace, delete, ^U,
// up/down through the history, enter; ^D on an empty line is end of
// input. The first echoed bytes of input were typed ahead and echoed by
// the console already, before the prompt: a whole line of them is
// taken as it is, part of one is shown again after the prompt.
// Returns 0 with a line in buf (ending in \n), -1 at end of input, -2
// if ^C interrupted it.

#define ESC 0x1b

static char *line;    // the line being edited, in getcmd's buf
static int len, pos;  // its length; the cursor

static void
putstr(char *s, int n)
{
  write(2, s, n);
}

// The screen shows the line up to from, with the cursor there, and
// oldlen characters in all: show the rest of the line, blank out what
// is left of the old one, and put the cursor at pos.
static void
redraw(int from, int oldlen)
{
  int end = len > oldlen ? len : oldlen;

  putstr(line + from, len - from);
  for(int i = len; i < oldlen; i++)
    putstr(" ", 1);
  for(int i = end; i > pos; i--)
    putstr("\b", 1);
}

static void
moveto(int to)
{
  for(; pos > to; pos--)
    putstr("\b", 1);
  if(to > pos)
    putstr(line + pos, to - pos);
  pos = to;
}

// Replace the line with s (up to a newline).
static void
setline(char *s, int nbuf)
{
  int oldlen = len;

  moveto(0);
  for(len = 0; s[len] && s[len] != '\n' && len < nbuf - 2; len++)
    line[len] = s[len];
  pos = len;
  redraw(0, oldlen);
}

// Insert n characters of s at the cursor.
static void
insert(char *s, int n, int nbuf)
{
  int i;

  for(i = 0; i < n && len < nbuf - 2; i++){
    memmove(line + pos + 1, line + pos, len - pos);
    line[pos++] = s[i];
    len++;
  }
  putstr(line + pos - i, i);
  redraw(pos, len);
}

// Tab completion: the names that may complete the word at the cursor.
#define NMATCH 64
static char matches[NMATCH][DIRSIZ+1];
static int matchdir[NMATCH];   // a directory: completes with '/'
static int nmatch;

// Add the names in directory dir that start with prefix (n bytes); for
// a command (cmd), only files. Names starting with '.' only if prefix
// does.
static void
addmatches(char *dir, char *prefix, int n, int cmd)
{
  char path[128], name[DIRSIZ+1];
  struct dirent de;
  struct stat st;
  int fd;

  if((fd = open(*dir ? dir : ".", O_RDONLY)) < 0)
    return;
  while(read(fd, &de, sizeof(de)) == sizeof(de) && nmatch < NMATCH){
    if(de.inum == 0)
      continue;
    memmove(name, de.name, DIRSIZ);
    name[DIRSIZ] = 0;
    if(strlen(name) < n || memcmp(name, prefix, n) != 0 || (name[0] == '.' && prefix[0] != '.'))
      continue;
    if(strlen(dir) + 1 + strlen(name) + 1 > sizeof(path))
      continue;
    strcpy(path, *dir ? dir : ".");
    strcpy(path + strlen(path), "/");
    strcpy(path + strlen(path), name);
    int isdir = stat(path, &st) == 0 && st.type == T_DIR;
    if(cmd && (isdir || st.type == T_DEVICE))
      continue;
    int dup = 0;
    for(int i = 0; i < nmatch; i++)
      dup |= strcmp(matches[i], name) == 0;
    if(!dup){
      strcpy(matches[nmatch], name);
      matchdir[nmatch++] = isdir;
    }
  }
  close(fd);
}

// Complete the word before the cursor: a command name (first word of
// a command, from PATH) or a file name. Fill in what all matches
// share; with one match, also '/' or ' ' after it. If that adds
// nothing, list the matches and show the line again.
static void
complete(int nbuf)
{
  int ws = pos, i;
  char word[100], dir[100];

  while(ws > 0 && !strchr(" \t|;&<>()", line[ws-1]))
    ws--;
  for(i = ws; i > 0 && (line[i-1] == ' ' || line[i-1] == '\t'); i--)
    ;
  int cmd = i == 0 || strchr("|;&(", line[i-1]);
  int n = pos - ws;
  memmove(word, line + ws, n);
  word[n] = 0;

  nmatch = 0;
  char *slash = 0;
  for(char *s = word; *s; s++)
    if(*s == '/')
      slash = s;
  char *prefix = word;
  if(slash){
    // a path: look in its directory
    int dn = slash - word;
    memmove(dir, word, dn);
    dir[dn] = 0;
    if(dn == 0)
      strcpy(dir, "/");
    prefix = slash + 1;
    addmatches(dir, prefix, strlen(prefix), 0);
  } else if(cmd){
    for(char *d = path; ; d++){
      char *end = strchr(d, ':');
      int dn = end ? end - d : strlen(d);
      memmove(dir, d, dn);
      dir[dn] = 0;
      addmatches(dir, prefix, strlen(prefix), 1);
      if(end == 0)
        break;
      d = end;
    }
  } else {
    addmatches("", prefix, strlen(prefix), 0);
  }
  if(nmatch == 0)
    return;

  // what all the matches have in common, past the prefix
  int have = strlen(prefix), common = strlen(matches[0]);
  for(i = 1; i < nmatch; i++){
    int j = 0;
    while(j < common && matches[i][j] == matches[0][j])
      j++;
    common = j;
  }
  int added = common - have;
  insert(matches[0] + have, added, nbuf);
  if(nmatch == 1){
    insert(matchdir[0] ? "/" : " ", 1, nbuf);
  } else if(added == 0){
    putstr("\n", 1);
    for(i = 0; i < nmatch; i++){
      putstr(matches[i], strlen(matches[i]));
      putstr(matchdir[i] ? "/  " : "  ", matchdir[i] ? 3 : 2);
    }
    putstr("\n$ ", 3);
    putstr(line, len);
    for(i = len; i > pos; i--)
      putstr("\b", 1);
  }
}

int
editline(char *buf, int nbuf, int echoed)
{
  static char saved[100];   // the new line, while browsing the history
  int hi = nhist;           // history entry shown; nhist: the new line
  char c, seq[2];

  line = buf;
  len = pos = 0;
  for(;;){
    int r = read(0, &c, 1);
    if(r < 0)
      return -2;   // ^C: the console printed ^C and a newline
    if(r == 0)
      return -1;
    if(echoed > 0){
      // typed ahead: echoed before the prompt
      echoed--;
      if(c == '\r' || c == '\n'){
        line[len++] = '\n';
        return 0;
      }
      if(len < nbuf - 2)
        line[len++] = c;
      pos = len;
      if(echoed == 0)
        putstr(line, len);
      continue;
    }

    if(c == '\r' || c == '\n'){
      moveto(len);
      putstr("\n", 1);
      line[len++] = '\n';
      line[len] = 0;
      return 0;
    }
    if(c == C('D') && len == 0)
      return -1;
    if(c == ESC){
      if(read(0, &seq[0], 1) != 1 || read(0, &seq[1], 1) != 1)
        return -2;
      if(seq[0] != '[' && seq[0] != 'O')
        continue;
      c = seq[1];
      if(c >= '0' && c <= '9'){
        // ESC [ n ~: 1 home, 3 delete, 4 end
        char t;
        if(read(0, &t, 1) != 1)
          return -2;
        c = c == '1' ? 'H' : c == '3' ? 'X' : c == '4' ? 'F' : 0;
      }
      if(c == 'A' && hi > 0 && hi > nhist - NHIST){
        if(hi == nhist){
          memmove(saved, line, len);
          saved[len] = 0;
        }
        hi--;
        setline(hist[hi % NHIST], nbuf);
      } else if(c == 'B' && hi < nhist){
        hi++;
        setline(hi == nhist ? saved : hist[hi % NHIST], nbuf);
      } else if(c == 'C' && pos < len){
        moveto(pos + 1);
      } else if(c == 'D' && pos > 0){
        moveto(pos - 1);
      } else if(c == 'H'){
        moveto(0);
      } else if(c == 'F'){
        moveto(len);
      } else if(c == 'X' && pos < len){
        memmove(line + pos, line + pos + 1, len - pos - 1);
        len--;
        redraw(pos, len + 1);
      }
      continue;
    }
    if(c == C('A')){
      moveto(0);
    } else if(c == C('E')){
      moveto(len);
    } else if(c == C('U')){
      setline("", nbuf);
    } else if(c == '\b' || c == 0x7f){
      if(pos > 0){
        moveto(pos - 1);
        memmove(line + pos, line + pos + 1, len - pos - 1);
        len--;
        redraw(pos, len + 1);
      }
    } else if(c == '\t'){
      complete(nbuf);
    } else if(c >= ' ' && c < 0x7f){
      insert(&c, 1, nbuf);
    }
  }
}

void
histadd(char *buf)
{
  char *s;

  for(s = buf; *s == ' ' || *s == '\t'; s++)
    ;
  if(*s == '\n' || *s == '\r' || *s == 0)
    return;
  strcpy(hist[nhist++ % NHIST], buf);
}

// sh [script [args]]: with a script, run its commands (#!/sh in a
// file makes it one), otherwise read commands from the console.
int
main(int argc, char *argv[])
{
  static char buf[100];
  int fd, in = 0;

  // Ensure that three file descriptors are open.
  while((fd = open("console", O_RDWR)) >= 0){
    if(fd >= 3){
      close(fd);
      break;
    }
  }

  if(argc > 1 && (in = open(argv[1], O_RDONLY)) < 0){
    fprintf(2, "sh: cannot open %s\n", argv[1]);
    exit(1);
  }

  // ^C interrupts commands, not an interactive shell: it shows a new
  // prompt instead (see getcmd()).
  if(in == 0)
    signal(SIGINT, interrupt);

  // Read and run input commands.
  while(getcmd(in, in == 0, buf, sizeof(buf)) >= 0){
    if(histexpand(buf) < 0)
      continue;
    histadd(buf);
    if(memcmp(buf, "history", 7) == 0 && strchr(" \t\r\n", buf[7])){
      for(int i = nhist > NHIST ? nhist - NHIST : 0; i < nhist; i++){
        char *line = hist[i % NHIST];
        int len = strlen(line);
        printf("%d  %s%s", i + 1, line, len && line[len-1] == '\n' ? "" : "\n");
      }
      continue;
    }
    if(buf[0] == 'c' && buf[1] == 'd' &&
       (buf[2] == ' ' || buf[2] == '\n' || buf[2] == 0)){
      // Chdir must be called by the parent, not the child.
      char *dir = buf + 2;
      while(*dir == ' ')
        dir++;
      if(dir[strlen(dir)-1] == '\n')
        dir[strlen(dir)-1] = 0;  // chop \n
      if(*dir == 0)
        dir = "/";  // plain cd: go to the root
      if(chdir(dir) < 0)
        fprintf(2, "cannot cd %s\n", dir);
      continue;
    }
    if(memcmp(buf, "PATH=", 5) == 0){
      // to the end of the line, without spaces
      char *s = buf + 5, *d = path;
      for(; *s && *s != '\n' && *s != '\r' && d < path + sizeof(path) - 1; s++)
        if(*s != ' ' && *s != '\t')
          *d++ = *s;
      *d = 0;
      continue;
    }
    int pid = fork1();
    if(pid == 0){
      if(in != 0)
        close(in);
      if(in == 0)
        signal(SIGINT, SIG_DFL);
      runcmd(parsecmd(buf));
    }
    // ^C interrupts waitpid() too: keep waiting until the command has
    // exited (it gets SIGINT as well).
    while(waitpid(pid, 0, 0) != pid && kill(pid, 0) == 0)
      ;
    // reap background commands that finished meanwhile
    while(waitpid(-1, 0, WNOHANG) > 0)
      ;
  }
  exit(0);
}

void
panic(char *s)
{
  fprintf(2, "%s\n", s);
  exit(1);
}

int
fork1(void)
{
  int pid;

  pid = fork();
  if(pid == -1)
    panic("fork");
  return pid;
}

// Constructors

struct cmd*
execcmd(void)
{
  struct execcmd *cmd;

  cmd = malloc(sizeof(*cmd));
  memset(cmd, 0, sizeof(*cmd));
  cmd->type = EXEC;
  return (struct cmd*)cmd;
}

struct cmd*
redircmd(struct cmd *subcmd, char *file, char *efile, int mode, int fd)
{
  struct redircmd *cmd;

  cmd = malloc(sizeof(*cmd));
  memset(cmd, 0, sizeof(*cmd));
  cmd->type = REDIR;
  cmd->cmd = subcmd;
  cmd->file = file;
  cmd->efile = efile;
  cmd->mode = mode;
  cmd->fd = fd;
  return (struct cmd*)cmd;
}

struct cmd*
pipecmd(struct cmd *left, struct cmd *right)
{
  struct pipecmd *cmd;

  cmd = malloc(sizeof(*cmd));
  memset(cmd, 0, sizeof(*cmd));
  cmd->type = PIPE;
  cmd->left = left;
  cmd->right = right;
  return (struct cmd*)cmd;
}

struct cmd*
listcmd(struct cmd *left, struct cmd *right)
{
  struct listcmd *cmd;

  cmd = malloc(sizeof(*cmd));
  memset(cmd, 0, sizeof(*cmd));
  cmd->type = LIST;
  cmd->left = left;
  cmd->right = right;
  return (struct cmd*)cmd;
}

struct cmd*
backcmd(struct cmd *subcmd)
{
  struct backcmd *cmd;

  cmd = malloc(sizeof(*cmd));
  memset(cmd, 0, sizeof(*cmd));
  cmd->type = BACK;
  cmd->cmd = subcmd;
  return (struct cmd*)cmd;
}

// Parsing

char whitespace[] = " \t\r\n\v";
char symbols[] = "<|>&;()";

int
gettoken(char **ps, char *es, char **q, char **eq)
{
  char *s;
  int ret;

  s = *ps;
  while(s < es && strchr(whitespace, *s))
    s++;
  if(q)
    *q = s;
  ret = *s;
  switch(*s){
  case 0:
    break;
  case '|':
  case '(':
  case ')':
  case ';':
  case '&':
  case '<':
    s++;
    break;
  case '>':
    s++;
    if(*s == '>'){
      ret = '+';
      s++;
    }
    break;
  default:
    ret = 'a';
    while(s < es && !strchr(whitespace, *s) && !strchr(symbols, *s))
      s++;
    break;
  }
  if(eq)
    *eq = s;

  while(s < es && strchr(whitespace, *s))
    s++;
  *ps = s;
  return ret;
}

int
peek(char **ps, char *es, char *toks)
{
  char *s;

  s = *ps;
  while(s < es && strchr(whitespace, *s))
    s++;
  *ps = s;
  return *s && strchr(toks, *s);
}

struct cmd *parseline(char**, char*);
struct cmd *parsepipe(char**, char*);
struct cmd *parseexec(char**, char*);
struct cmd *nulterminate(struct cmd*);

struct cmd*
parsecmd(char *s)
{
  char *es;
  struct cmd *cmd;

  es = s + strlen(s);
  cmd = parseline(&s, es);
  peek(&s, es, "");
  if(s != es){
    fprintf(2, "leftovers: %s\n", s);
    panic("syntax");
  }
  nulterminate(cmd);
  return cmd;
}

struct cmd*
parseline(char **ps, char *es)
{
  struct cmd *cmd;

  cmd = parsepipe(ps, es);
  while(peek(ps, es, "&")){
    gettoken(ps, es, 0, 0);
    cmd = backcmd(cmd);
  }
  if(peek(ps, es, ";")){
    gettoken(ps, es, 0, 0);
    cmd = listcmd(cmd, parseline(ps, es));
  }
  return cmd;
}

struct cmd*
parsepipe(char **ps, char *es)
{
  struct cmd *cmd;

  cmd = parseexec(ps, es);
  if(peek(ps, es, "|")){
    gettoken(ps, es, 0, 0);
    cmd = pipecmd(cmd, parsepipe(ps, es));
  }
  return cmd;
}

struct cmd*
parseredirs(struct cmd *cmd, char **ps, char *es)
{
  int tok;
  char *q, *eq;

  while(peek(ps, es, "<>")){
    tok = gettoken(ps, es, 0, 0);
    if(gettoken(ps, es, &q, &eq) != 'a')
      panic("missing file for redirection");
    switch(tok){
    case '<':
      cmd = redircmd(cmd, q, eq, O_RDONLY, 0);
      break;
    case '>':
      cmd = redircmd(cmd, q, eq, O_WRONLY|O_CREATE|O_TRUNC, 1);
      break;
    case '+':  // >>
      cmd = redircmd(cmd, q, eq, O_WRONLY|O_CREATE, 1);
      break;
    }
  }
  return cmd;
}

struct cmd*
parseblock(char **ps, char *es)
{
  struct cmd *cmd;

  if(!peek(ps, es, "("))
    panic("parseblock");
  gettoken(ps, es, 0, 0);
  cmd = parseline(ps, es);
  if(!peek(ps, es, ")"))
    panic("syntax - missing )");
  gettoken(ps, es, 0, 0);
  cmd = parseredirs(cmd, ps, es);
  return cmd;
}

struct cmd*
parseexec(char **ps, char *es)
{
  char *q, *eq;
  int tok, argc;
  struct execcmd *cmd;
  struct cmd *ret;

  if(peek(ps, es, "("))
    return parseblock(ps, es);

  ret = execcmd();
  cmd = (struct execcmd*)ret;

  argc = 0;
  ret = parseredirs(ret, ps, es);
  while(!peek(ps, es, "|)&;")){
    if((tok=gettoken(ps, es, &q, &eq)) == 0)
      break;
    if(tok != 'a')
      panic("syntax");
    cmd->argv[argc] = q;
    cmd->eargv[argc] = eq;
    argc++;
    if(argc >= MAXARGS)
      panic("too many args");
    ret = parseredirs(ret, ps, es);
  }
  cmd->argv[argc] = 0;
  cmd->eargv[argc] = 0;
  return ret;
}

// NUL-terminate all the counted strings.
struct cmd*
nulterminate(struct cmd *cmd)
{
  int i;
  struct backcmd *bcmd;
  struct execcmd *ecmd;
  struct listcmd *lcmd;
  struct pipecmd *pcmd;
  struct redircmd *rcmd;

  if(cmd == 0)
    return 0;

  switch(cmd->type){
  case EXEC:
    ecmd = (struct execcmd*)cmd;
    for(i=0; ecmd->argv[i]; i++)
      *ecmd->eargv[i] = 0;
    break;

  case REDIR:
    rcmd = (struct redircmd*)cmd;
    nulterminate(rcmd->cmd);
    *rcmd->efile = 0;
    break;

  case PIPE:
    pcmd = (struct pipecmd*)cmd;
    nulterminate(pcmd->left);
    nulterminate(pcmd->right);
    break;

  case LIST:
    lcmd = (struct listcmd*)cmd;
    nulterminate(lcmd->left);
    nulterminate(lcmd->right);
    break;

  case BACK:
    bcmd = (struct backcmd*)cmd;
    nulterminate(bcmd->cmd);
    break;
  }
  return cmd;
}
