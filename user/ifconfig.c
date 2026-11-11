// ifconfig: the network interface and its addresses.

#include "types.h"
#include "user.h"
#include "socket.h"

static void
addr(char *label, uint32 a)
{
  if(a)
    printf("  %s %d.%d.%d.%d\n", label, a >> 24, (a >> 16) & 0xff, (a >> 8) & 0xff, a & 0xff);
  else
    printf("  %s none yet\n", label);
}

int
main(void)
{
  struct netinfo ni;
  if(netinfo(&ni) < 0 || ni.name[0] == 0){
    printf("no network interface\n");
    exit(1);
  }
  printf("%s: mac %x:%x:%x:%x:%x:%x\n", ni.name, ni.mac[0], ni.mac[1], ni.mac[2], ni.mac[3],
         ni.mac[4], ni.mac[5]);
  int bits = 0;
  for(uint32 m = ni.mask; m & 0x80000000; m <<= 1)
    bits++;
  if(ni.ip)
    printf("  inet %d.%d.%d.%d/%d\n", ni.ip >> 24, (ni.ip >> 16) & 0xff, (ni.ip >> 8) & 0xff,
           ni.ip & 0xff, bits);
  else
    printf("  inet none yet\n");
  addr("gateway", ni.gateway);
  addr("dns", ni.dns);
  exit(0);
}
