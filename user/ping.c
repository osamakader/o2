// ping [-c count] address: send ICMP echo requests, a second apart,
// and show the replies and their round-trip time.

#include "types.h"
#include "user.h"
#include "socket.h"
#include "time.h"

static uint64
now_us(void)
{
  struct timespec ts;
  clock_gettime(CLOCK_MONOTONIC, &ts);
  return ts.tv_sec * 1000000 + ts.tv_nsec / 1000;
}

int
main(int argc, char *argv[])
{
  int count = 4, i = 1;
  struct sockaddr_in to = { .sin_family = AF_INET };

  if(argc >= 3 && strcmp(argv[1], "-c") == 0){
    count = atoi(argv[2]);
    i = 3;
  }
  if(i != argc - 1 || !inet_aton(argv[i], &to.sin_addr)){
    fprintf(2, "usage: ping [-c count] a.b.c.d\n");
    exit(1);
  }
  int fd = socket(AF_INET, SOCK_DGRAM, IPPROTO_ICMP);
  if(fd < 0){
    fprintf(2, "ping: no network\n");
    exit(1);
  }
  struct timeval tv = { 1, 0 };
  setsockopt(fd, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(tv));

  int got = 0;
  for(int seq = 1; seq <= count; seq++){
    char msg[64], reply[128];
    memset(msg, 0, sizeof(msg));
    msg[0] = 8;            // echo request; the kernel sets id and checksum
    msg[6] = seq >> 8;
    msg[7] = seq;
    for(int j = 8; j < sizeof(msg); j++)
      msg[j] = j;
    uint64 t0 = now_us();
    if(sendto(fd, msg, sizeof(msg), 0, &to, sizeof(to)) < 0){
      printf("ping: send failed\n");
      continue;
    }
    for(;;){
      struct sockaddr_in from;
      int n = recvfrom(fd, reply, sizeof(reply), 0, &from, 0);
      if(n < 0){
        printf("no reply for seq %d\n", seq);
        break;
      }
      if(n >= 8 && ((reply[6] & 0xff) << 8 | (reply[7] & 0xff)) == seq){
        uint64 t = now_us() - t0;
        printf("%d bytes from %s: seq %d time %lu.%03lu ms\n", n, argv[i], seq,
               t / 1000, t % 1000);
        got++;
        if(seq < count)
          pause(100 - t / 10000 > 0 ? 100 - t / 10000 : 0);
        break;
      }
    }
  }
  printf("%d sent, %d received\n", count, got);
  exit(got == count ? 0 : 1);
}
