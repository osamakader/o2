// tcptest a.b.c.d port [bytes]: send bytes (default 100000) over a TCP
// connection to an echo server there, reading the echo as it comes,
// and check it. Then a connection to port 1 there, which should be
// refused.

#include "types.h"
#include "user.h"
#include "socket.h"
#include "poll.h"

static char
pattern(int i)
{
  return 'a' + (i * 7 + i / 26) % 26;
}

int
main(int argc, char *argv[])
{
  struct sockaddr_in to = { .sin_family = AF_INET };
  if(argc < 3 || !inet_aton(argv[1], &to.sin_addr)){
    fprintf(2, "usage: tcptest a.b.c.d port [bytes]\n");
    exit(1);
  }
  to.sin_port = htons(atoi(argv[2]));
  int total = argc > 3 ? atoi(argv[3]) : 100000;

  int fd = socket(AF_INET, SOCK_STREAM, 0);
  if(fd < 0 || connect(fd, &to, sizeof(to)) < 0){
    fprintf(2, "tcptest: cannot connect\n");
    exit(1);
  }
  int t0 = uptime(), sent = 0, got = 0, bad = 0;
  static char buf[4096];
  while(got < total){
    struct pollfd p = { fd, POLLIN | (sent < total ? POLLOUT : 0), 0 };
    if(poll(&p, 1, 30000) <= 0){
      printf("tcptest: stuck at %d of %d sent, %d received\n", sent, total, got);
      exit(1);
    }
    if((p.revents & POLLOUT) && sent < total){
      int n = total - sent < 3000 ? total - sent : 3000;
      for(int i = 0; i < n; i++)
        buf[i] = pattern(sent + i);
      int w = write(fd, buf, n);
      if(w <= 0){
        printf("tcptest: write failed\n");
        exit(1);
      }
      sent += w;
    }
    if(p.revents & (POLLIN | POLLHUP)){
      int n = read(fd, buf, sizeof(buf));
      if(n <= 0){
        printf("tcptest: connection ended at %d of %d\n", got, total);
        exit(1);
      }
      for(int i = 0; i < n; i++)
        bad += buf[i] != pattern(got + i);
      got += n;
    }
  }
  close(fd);
  int ms = (uptime() - t0) * 10;
  printf("tcptest: %d bytes echoed, %d wrong, %d ms\n", got, bad, ms);

  // nobody there
  to.sin_port = htons(1);
  fd = socket(AF_INET, SOCK_STREAM, 0);
  if(connect(fd, &to, sizeof(to)) == 0)
    printf("tcptest: port 1 accepted?\n");
  else
    printf("tcptest: port 1 refused\n");
  close(fd);
  exit(bad ? 1 : 0);
}
