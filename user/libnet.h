#ifndef LIBNET_H
#define LIBNET_H

// libnet.c: DNS, TCP by host name, HTTP GET.

#include "types.h"
#include "socket.h"

#define DNS_FAIL     (-1)
#define DNS_NOANSWER (-2)
#define DNS_NONAME   (-3)

int dns_lookup(const char *name, const char *server, struct in_addr *a, int max);
int resolve(const char *host, struct in_addr *a);
int tcp_dial(const char *host, int port);

struct url {
  char host[128];
  int port;
  char path[384];
};

#define HTTP_BADURL   (-1)
#define HTTP_HTTPS    (-2)
#define HTTP_NOHOST   (-3)
#define HTTP_NOCONN   (-4)
#define HTTP_BADREPLY (-5)
#define HTTP_LOOP     (-6)

struct http {
  int fd;
  int status;              // 200, 404, ...
  char url[512];           // where it came from, after redirects
  char header[2048];       // the status line and fields
  char buf[4096];          // read, from pos on not yet taken
  int len, pos;
};

int url_parse(const char *url, struct url *u);
int http_get(const char *url, struct http *h);
int http_read(struct http *h, char *p, int n);
char *http_field(struct http *h, const char *name, char *val, int max);
void http_close(struct http *h);
char *http_error(int r);

#endif
