// nslookup name [server]: ask a DNS server (default the network's)
// for name's IPv4 addresses.

#include "types.h"
#include "user.h"
#include "libnet.h"

int
main(int argc, char *argv[])
{
  if(argc < 2 || argc > 3){
    fprintf(2, "usage: nslookup name [a.b.c.d]\n");
    exit(1);
  }
  struct in_addr a[16];
  int n = dns_lookup(argv[1], argc == 3 ? argv[2] : 0, a, 16);
  if(n == DNS_NONAME)
    printf("%s: no such name\n", argv[1]);
  else if(n == DNS_NOANSWER)
    fprintf(2, "nslookup: no answer\n");
  else if(n < 0)
    fprintf(2, "nslookup: failed (bad name or server, or no network)\n");
  else if(n == 0)
    printf("%s: no IPv4 address\n", argv[1]);
  for(int i = 0; i < n; i++){
    uchar *p = (uchar *)&a[i];
    printf("%s: %d.%d.%d.%d\n", argv[1], p[0], p[1], p[2], p[3]);
  }
  exit(n > 0 ? 0 : 1);
}
