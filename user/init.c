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

  // nothing else to do yet; reap orphans forever
  for(;;)
    wait(0);
}
