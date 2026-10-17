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

  // nothing else to do yet; reap orphans forever
  for(;;)
    wait(0);
}
