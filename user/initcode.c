// The first user program, embedded in the kernel: run /init from the
// file system.

#include "types.h"
#include "user.h"

int main(void)
{
  char *argv[] = { "init", 0 };

  exec("/init", argv);
  exit(1);
}
