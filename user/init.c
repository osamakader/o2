// init: the initial user-level program. Sets up the console as
// stdin/stdout/stderr and keeps a shell running. From xv6.

#include "types.h"
#include "stat.h"
#include "user.h"
#include "fcntl.h"
#include "file.h"
#include "fb.h"
#include "input.h"
#include "fs.h"
#include "time.h"

char *argv[] = { "sh", 0 };

// Without an RTC (the Raspberry Pi) the clock starts at the epoch.
// Start it instead at the newest time a file in / was changed: the
// file system's build, or the last write. Wrong, but not by decades,
// and it doesn't go back; date -s sets it right.
static void
clockfromfiles(void)
{
  if(time() >= 946684800)   // 2000: there is an RTC
    return;
  int fd = open("/", O_RDONLY);
  if(fd < 0)
    return;
  uint64 newest = 0;
  struct dirent de;
  struct stat st;
  char path[DIRSIZ + 2] = "/";
  while(read(fd, &de, sizeof(de)) == sizeof(de)){
    if(de.inum == 0)
      continue;
    memmove(path + 1, de.name, DIRSIZ);
    path[DIRSIZ + 1] = 0;
    if(lstat(path, &st) == 0 && st.mtime > newest)
      newest = st.mtime;
  }
  close(fd);
  if(newest > time()){
    struct timespec ts = { newest, 0 };
    clock_settime(CLOCK_REALTIME, &ts);
    printf("init: no RTC; the clock starts at the newest file's time (date -s sets it)\n");
  }
}

int
main(void)
{
  int pid, wpid;

  if(open("console", O_RDWR) < 0){
    mknod("console", CONSOLE, 0);
    open("console", O_RDWR);
  }
  dup(0);  // stdout
  dup(0);  // stderr

  clockfromfiles();

  // the framebuffer, for programs that draw (fb.h)
  struct stat st;
  if(stat("fb", &st) < 0)
    mknod("fb", FB, 0);
  if(stat("input", &st) < 0)
    mknod("input", INPUT, 0);

  // the boot animation, if there is a screen to show it on
  char *splash[] = { "splash", 0 };
  if((pid = fork()) == 0){
    exec("splash", splash);
    exit(0);
  }
  if(pid > 0)
    waitpid(pid, 0, 0);

  for(;;){
    printf("init: starting sh\n");
    pid = fork();
    if(pid < 0){
      printf("init: fork failed\n");
      exit(1);
    }
    if(pid == 0){
      exec("sh", argv);
      printf("init: exec sh failed\n");
      exit(1);
    }

    for(;;){
      // this call to wait() returns if the shell exits,
      // or if a parentless process exits.
      wpid = wait((int *) 0);
      if(wpid == pid){
        // the shell exited; restart it.
        break;
      } else if(wpid < 0){
        printf("init: wait returned an error\n");
        exit(1);
      } else {
        // it was a parentless process; do nothing.
      }
    }
  }
}
