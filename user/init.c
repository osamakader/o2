// The first user program.

#include "types.h"
#include "user.h"

int main(void)
{
  printf("init: hello from user space, pid %d\n", getpid());
  return 0;
}
