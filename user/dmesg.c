// dmesg: print the kernel's message log.

#include "types.h"
#include "user.h"

static char buf[16384];

int
main(void)
{
  int n = klog(buf, sizeof(buf));
  if(n < 0){
    fprintf(2, "dmesg: klog failed\n");
    exit(1);
  }
  write(1, buf, n);
  exit(0);
}
