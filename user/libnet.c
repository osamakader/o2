// libnet: names and the web, for programs that use the network (fetch,
// nslookup): DNS lookups (qemu's server, 10.0.2.3), TCP connections by
// host name, and HTTP/1.0 GET with redirects. http:// only: no TLS.

#include "types.h"
#include "user.h"
#include "socket.h"
#include "libnet.h"

#define DNS_SERVER "10.0.2.3"

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

// Ask the DNS server (0: qemu's) for name's IPv4 addresses, up to max
// of them into a. Returns how many, or DNS_NONAME, DNS_NOANSWER,
// DNS_FAIL (bad name, no network, server failure).
int
dns_lookup(const char *name, const char *server, struct in_addr *a, int max)
{
  struct sockaddr_in to = { .sin_family = AF_INET, .sin_port = htons(53) };
  if(!inet_aton(server ? server : DNS_SERVER, &to.sin_addr))
    return DNS_FAIL;

  uchar q[512], r[512];
  memset(q, 0, 12);
  int id = (getpid() * 7919 + uptime()) & 0xffff;
  q[0] = id >> 8;
  q[1] = id;
  q[2] = 0x01;          // recursion desired
  q[5] = 1;             // one question
  uchar *p = (uchar *)put_name((char *)q + 12, (char *)q + sizeof(q) - 4, name);
  if(p == 0)
    return DNS_FAIL;
  *p++ = 0; *p++ = 1;   // type A
  *p++ = 0; *p++ = 1;   // class IN

  int fd = socket(AF_INET, SOCK_DGRAM, 0);
  if(fd < 0)
    return DNS_FAIL;
  struct timeval tv = { 3, 0 };
  setsockopt(fd, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(tv));
  int n = -1;
  for(int try = 0; try < 2 && n < 0; try++){
    if(sendto(fd, q, p - q, 0, &to, sizeof(to)) < 0)
      break;
    // an answer to this question, not a late one to another
    while((n = recvfrom(fd, r, sizeof(r), 0, 0, 0)) >= 0 &&
          (n < 12 || r[0] != q[0] || r[1] != q[1]))
      ;
  }
  close(fd);
  if(n < 12)
    return DNS_NOANSWER;
  int rcode = r[3] & 0xf, qd = r[4] << 8 | r[5], an = r[6] << 8 | r[7];
  if(rcode == 3)
    return DNS_NONAME;
  if(rcode != 0)
    return DNS_FAIL;
  uchar *end = r + n;
  p = r + 12;
  for(int i = 0; i < qd && p; i++)
    p = skip_name(p, end) ? skip_name(p, end) + 4 : 0;
  int found = 0;
  for(int i = 0; i < an && p && p + 10 <= end && found < max; i++){
    p = skip_name(p, end);
    if(p == 0 || p + 10 > end)
      break;
    int type = p[0] << 8 | p[1], len = p[8] << 8 | p[9];
    p += 10;
    if(p + len > end)
      break;
    if(type == 1 && len == 4)
      memmove(&a[found++], p, 4);   // network byte order, as it comes
    p += len;
  }
  return found;
}

// host, a name or a.b.c.d, as an address. Returns 0, or -1.
int
resolve(const char *host, struct in_addr *a)
{
  if(inet_aton(host, a))
    return 0;
  return dns_lookup(host, 0, a, 1) == 1 ? 0 : -1;
}

// A TCP connection to host's port. Returns its fd, or -1.
int
tcp_dial(const char *host, int port)
{
  struct sockaddr_in to = { .sin_family = AF_INET, .sin_port = htons(port) };
  if(resolve(host, &to.sin_addr) < 0)
    return -1;
  int fd = socket(AF_INET, SOCK_STREAM, 0);
  if(fd < 0)
    return -1;
  if(connect(fd, &to, sizeof(to)) < 0){
    close(fd);
    return -1;
  }
  return fd;
}

// Split "http://host[:port][/path]" into u. Returns 0, -1 if it isn't
// one (HTTP_HTTPS for https://).
int
url_parse(const char *url, struct url *u)
{
  if(strncmp(url, "https://", 8) == 0)
    return HTTP_HTTPS;
  if(strncmp(url, "http://", 7) == 0)
    url += 7;
  else if(strstr(url, "://"))
    return -1;
  const char *h = url;
  int n = 0;
  while(*url && *url != '/' && *url != ':' && *url != '?' && *url != '#')
    url++, n++;
  if(n == 0 || n >= sizeof(u->host))
    return -1;
  memmove(u->host, h, n);
  u->host[n] = 0;
  u->port = 80;
  if(*url == ':'){
    u->port = atoi(++url);
    while(*url >= '0' && *url <= '9')
      url++;
  }
  if(*url == '#')
    url += strlen(url);
  if(strlen(url) + 2 > sizeof(u->path))
    return -1;
  u->path[0] = 0;
  if(*url != '/')
    strcpy(u->path, "/");
  strcpy(u->path + strlen(u->path), url);
  // no fragment
  char *f = strchr(u->path, '#');
  if(f)
    *f = 0;
  return 0;
}

// Read the response's header into h; what came after it stays in
// h->buf. Returns 0, or -1.
static int
read_header(struct http *h)
{
  h->len = h->pos = 0;
  for(;;){
    if(h->len == sizeof(h->buf) - 1)
      return -1;   // too long a header
    int n = read(h->fd, h->buf + h->len, sizeof(h->buf) - 1 - h->len);
    if(n <= 0)
      return -1;
    h->len += n;
    h->buf[h->len] = 0;
    char *e = strstr(h->buf, "\r\n\r\n");
    int skip = 4;
    if(e == 0 && (e = strstr(h->buf, "\n\n")) != 0)
      skip = 2;
    if(e == 0)
      continue;
    int hlen = e - h->buf;
    memmove(h->header, h->buf, hlen < sizeof(h->header) - 1 ? hlen : sizeof(h->header) - 1);
    h->header[hlen < sizeof(h->header) - 1 ? hlen : sizeof(h->header) - 1] = 0;
    h->pos = hlen + skip;
    return 0;
  }
}

// The value of header field name (lower case, with its ':') into val
// (max bytes); val, or 0 if there is none.
char *
http_field(struct http *h, const char *name, char *val, int max)
{
  int n = strlen(name);
  for(char *l = h->header; l; l = strchr(l, '\n'), l = l ? l + 1 : 0){
    int i;
    for(i = 0; i < n && l[i] && (l[i] | 0x20) == (name[i] | 0x20); i++)
      ;
    if(i < n)
      continue;
    char *v = l + n;
    while(*v == ' ' || *v == '\t')
      v++;
    int k = 0;
    while(v[k] && v[k] != '\r' && v[k] != '\n' && k < max - 1)
      k++;
    memmove(val, v, k);
    val[k] = 0;
    return val;
  }
  return 0;
}

// GET url, following up to 5 redirects. On success h->fd is the
// connection, h->status the status code, the header in h->header and
// h->url where it came from in the end; the body follows through
// http_read(). Returns 0, or HTTP_BADURL, HTTP_HTTPS, HTTP_NOHOST,
// HTTP_NOCONN, HTTP_BADREPLY, HTTP_LOOP.
int
http_get(const char *url, struct http *h)
{
  // big for a stack: static (programs have one thread)
  static char cur[sizeof(h->url)], req[600], loc[sizeof(h->url)];
  if(strlen(url) >= sizeof(cur))
    return HTTP_BADURL;
  strcpy(cur, url);
  for(int hops = 0; hops < 6; hops++){
    static struct url u;
    int r = url_parse(cur, &u);
    if(r < 0)
      return r == HTTP_HTTPS ? HTTP_HTTPS : HTTP_BADURL;
    struct in_addr a;
    if(resolve(u.host, &a) < 0)
      return HTTP_NOHOST;
    struct sockaddr_in to = { .sin_family = AF_INET, .sin_port = htons(u.port), .sin_addr = a };
    h->fd = socket(AF_INET, SOCK_STREAM, 0);
    if(h->fd < 0 || connect(h->fd, &to, sizeof(to)) < 0){
      if(h->fd >= 0)
        close(h->fd);
      return HTTP_NOCONN;
    }
    // the request
    char *e = req;
    e += strlen(strcpy(e, "GET "));
    e += strlen(strcpy(e, u.path));
    e += strlen(strcpy(e, " HTTP/1.0\r\nHost: "));
    e += strlen(strcpy(e, u.host));
    if(u.port != 80){
      char p[8];
      int n = 0, v = u.port;
      char d[6];
      int nd = 0;
      do
        d[nd++] = '0' + v % 10;
      while((v /= 10) > 0);
      p[n++] = ':';
      while(nd > 0)
        p[n++] = d[--nd];
      p[n] = 0;
      e += strlen(strcpy(e, p));
    }
    e += strlen(strcpy(e, "\r\nUser-Agent: o2-fetch/1.0\r\nAccept: */*\r\n"
                          "Connection: close\r\n\r\n"));
    if(write(h->fd, req, e - req) != e - req || read_header(h) < 0){
      close(h->fd);
      return HTTP_BADREPLY;
    }
    // "HTTP/1.x 200 OK"
    char *sp = strchr(h->header, ' ');
    if(strncmp(h->header, "HTTP/", 5) != 0 || sp == 0){
      close(h->fd);
      return HTTP_BADREPLY;
    }
    h->status = atoi(sp + 1);
    strcpy(h->url, cur);
    if(h->status / 100 != 3 || !http_field(h, "location:", loc, sizeof(loc)))
      return 0;
    // a redirect: to a URL, or a path on the same host
    close(h->fd);
    if(loc[0] == '/'){
      char *p = strchr(cur + (strncmp(cur, "http://", 7) == 0 ? 7 : 0), '/');
      if(p)
        *p = 0;
      if(strlen(cur) + strlen(loc) >= sizeof(cur))
        return HTTP_BADURL;
      strcpy(cur + strlen(cur), loc);
    } else {
      strcpy(cur, loc);
    }
  }
  return HTTP_LOOP;
}

// The body: up to n bytes into p. Returns how many, 0 at its end, -1
// on errors.
int
http_read(struct http *h, char *p, int n)
{
  if(h->pos < h->len){
    int k = h->len - h->pos < n ? h->len - h->pos : n;
    memmove(p, h->buf + h->pos, k);
    h->pos += k;
    return k;
  }
  return read(h->fd, p, n);
}

void
http_close(struct http *h)
{
  close(h->fd);
}

// A message for an http_get() error.
char *
http_error(int r)
{
  switch(r){
  case HTTP_BADURL: return "not an http:// URL";
  case HTTP_HTTPS: return "https isn't supported (no TLS)";
  case HTTP_NOHOST: return "no such host";
  case HTTP_NOCONN: return "cannot connect";
  case HTTP_BADREPLY: return "no HTTP reply";
  case HTTP_LOOP: return "too many redirects";
  }
  return "failed";
}
