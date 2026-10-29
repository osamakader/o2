// reboot: commit the file system and restart.

#include "types.h"
#include "user.h"
#include "syscall.h"

int
main(void)
{
  reboot(REBOOT_RESTART);
  fprintf(2, "reboot: failed\n");
  exit(1);
}
