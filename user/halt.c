// halt: commit the file system and power off.

#include "types.h"
#include "user.h"
#include "syscall.h"

int
main(void)
{
  reboot(REBOOT_OFF);
  fprintf(2, "halt: failed\n");
  exit(1);
}
