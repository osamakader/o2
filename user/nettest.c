// nettest a.b.c.d port [count]: send count (default 20) UDP datagrams
// to an echo server there and check that each comes back.

#include "types.h"
#include "user.h"
#include "socket.h"

int
main(int argc, char *argv[])
{
  struct sockaddr_in to = { .sin_family = AF_INET };

  if(argc < 3 || !inet_aton(argv[1], &to.sin_addr)){
    fprintf(2, "usage: nettest a.b.c.d port [count]\n");
    exit(1);
  }
  to.sin_port = htons(atoi(argv[2]));
  int count = argc > 3 ? atoi(argv[3]) : 20;

  int fd = socket(AF_INET, SOCK_DGRAM, 0);
  if(fd < 0){
    fprintf(2, "nettest: no network\n");
    exit(1);
  }
  struct timeval tv = { 2, 0 };
  setsockopt(fd, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(tv));

  int ok = 0;
  for(int i = 0; i < count; i++){
    char msg[1024], reply[1100];
    // sizes from 1 byte to 1000
    int n = 1 + (i * 997) % 1000;
    for(int j = 0; j < n; j++)
      msg[j] = 'a' + (i + j) % 26;
    if(sendto(fd, msg, n, 0, &to, sizeof(to)) != n){
      printf("nettest: send %d failed\n", i);
      continue;
    }
    struct sockaddr_in from;
    int len = sizeof(from);
    int r = recvfrom(fd, reply, sizeof(reply), 0, &from, &len);
    if(r != n || memcmp(msg, reply, n) != 0 || from.sin_addr.s_addr != to.sin_addr.s_addr ||
       from.sin_port != to.sin_port)
      printf("nettest: datagram %d: %s\n", i, r < 0 ? "no reply" : "wrong reply");
    else
      ok++;
  }
  printf("nettest: %d of %d echoed\n", ok, count);
  exit(ok == count ? 0 : 1);
}
