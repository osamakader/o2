// nslookup name [server]: ask a DNS server (default qemu's, 10.0.2.3)
// for name's IPv4 addresses.

#include "types.h"
#include "user.h"
#include "socket.h"

// Append name in DNS form (length-prefixed labels) at p; returns the
// end, or 0 if it doesn't fit.
static char *
put_name(char *p, char *end, const char *name)
{
  while(*name){
    const char *dot = strchr(name, '.');
    int n = dot ? dot - name : strlen(name);
    if(n == 0 || n > 63 || p + 1 + n >= end)
      return 0;
    *p++ = n;
    memmove(p, name, n);
    p += n;
    name += n + (dot != 0);
  }
  *p++ = 0;
  return p;
}

// Skip a (possibly compressed) name at p; returns what follows, or 0.
static uchar *
skip_name(uchar *p, uchar *end)
{
  while(p < end){
    if(*p == 0)
      return p + 1;
    if((*p & 0xc0) == 0xc0)
      return p + 2;
    p += *p + 1;
  }
  return 0;
}

int
main(int argc, char *argv[])
{
  struct sockaddr_in to = { .sin_family = AF_INET, .sin_port = htons(53) };

  if(argc < 2 || argc > 3){
    fprintf(2, "usage: nslookup name [a.b.c.d]\n");
    exit(1);
  }
  if(!inet_aton(argc == 3 ? argv[2] : "10.0.2.3", &to.sin_addr)){
    fprintf(2, "nslookup: bad server address\n");
    exit(1);
  }

  uchar q[512], r[512];
  memset(q, 0, 12);
  int id = getpid() & 0xffff;
  q[0] = id >> 8;
  q[1] = id;
  q[2] = 0x01;          // recursion desired
  q[5] = 1;             // one question
  uchar *p = (uchar *)put_name((char *)q + 12, (char *)q + sizeof(q) - 4, argv[1]);
  if(p == 0){
    fprintf(2, "nslookup: bad name\n");
    exit(1);
  }
  *p++ = 0; *p++ = 1;   // type A
  *p++ = 0; *p++ = 1;   // class IN

  int fd = socket(AF_INET, SOCK_DGRAM, 0);
  if(fd < 0){
    fprintf(2, "nslookup: no network\n");
    exit(1);
  }
  struct timeval tv = { 3, 0 };
  setsockopt(fd, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(tv));
  if(sendto(fd, q, p - q, 0, &to, sizeof(to)) < 0){
    fprintf(2, "nslookup: send failed\n");
    exit(1);
  }
  int n = recvfrom(fd, r, sizeof(r), 0, 0, 0);
  if(n < 12 || r[0] != q[0] || r[1] != q[1]){
    fprintf(2, "nslookup: no answer\n");
    exit(1);
  }
  int rcode = r[3] & 0xf, qd = r[4] << 8 | r[5], an = r[6] << 8 | r[7];
  if(rcode != 0){
    printf("%s: %s\n", argv[1], rcode == 3 ? "no such name" : "server failure");
    exit(1);
  }
  uchar *end = r + n;
  p = r + 12;
  for(int i = 0; i < qd && p; i++)
    p = skip_name(p, end) ? skip_name(p, end) + 4 : 0;
  int found = 0;
  for(int i = 0; i < an && p && p + 10 <= end; i++){
    p = skip_name(p, end);
    if(p == 0 || p + 10 > end)
      break;
    int type = p[0] << 8 | p[1], len = p[8] << 8 | p[9];
    p += 10;
    if(p + len > end)
      break;
    if(type == 1 && len == 4){
      printf("%s: %d.%d.%d.%d\n", argv[1], p[0], p[1], p[2], p[3]);
      found++;
    }
    p += len;
  }
  if(!found)
    printf("%s: no IPv4 address\n", argv[1]);
  exit(found ? 0 : 1);
}
