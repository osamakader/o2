// The first user program.

#include "types.h"
#include "user.h"

int main(void)
{
  printf("init: hello from user space, pid %d\n", getpid());

  int pid = fork();
  if(pid < 0){
    printf("init: fork failed\n");
  } else if(pid == 0){
    printf("child: pid %d\n", getpid());
    exit(7);
  } else {
    int status = -1;
    int got = wait(&status);
    printf("init: child %d exited with status %d (fork returned %d)\n", got, status, pid);
  }

  // grow the heap, use it, shrink it back
  char *base = sbrk(0);
  char *a = sbrk(3 * 4096);
  if(a != base){
    printf("init: sbrk returned %p, expected %p\n", a, base);
  } else {
    for(int i = 0; i < 3 * 4096; i += 4096)
      a[i] = i / 4096 + 1;
    int sum = a[0] + a[4096] + a[2 * 4096];
    sbrk(-(3 * 4096));
    printf("init: sbrk ok, sum %d, size back to %p\n", sum, sbrk(0));
  }

  int t0 = uptime();
  sleep(25);
  printf("init: slept %d ticks\n", uptime() - t0);

  // no shell yet: echo console lines back until end of file, then
  // just reap orphans
  char line[64];
  for(;;){
    int n = read(0, line, sizeof(line) - 1);
    if(n <= 0)
      break;
    if(line[n-1] == '\n')
      n--;
    line[n] = 0;
    printf("init: got \"%s\"\n", line);
  }
  printf("init: console eof\n");
  for(;;){
    if(wait(0) < 0)
      sleep(100);
  }
}
