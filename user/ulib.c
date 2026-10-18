#include "types.h"
#include "user.h"
#include "time.h"
#include "stat.h"
#include "fcntl.h"
#include "syscall.h"

int main(int argc, char *argv[]);

// Entry point of every user program: the kernel starts it with argc
// in x0 and argv in x1. main's return value becomes the exit status.
void _start(int argc, char *argv[])
{
  exit(main(argc, argv));
}

char* strcpy(char *s, const char *t)
{
  char *os;

  os = s;
  while((*s++ = *t++) != 0)
    ;
  return os;
}

int strcmp(const char *p, const char *q)
{
  while(*p && *p == *q)
    p++, q++;
  return (uchar)*p - (uchar)*q;
}

uint strlen(const char *s)
{
  int n;

  for(n = 0; s[n]; n++)
    ;
  return n;
}

void* memset(void *dst, int c, uint n)
{
  char *cdst = (char *) dst;
  int i;
  for(i = 0; i < n; i++){
    cdst[i] = c;
  }
  return dst;
}

char* gets(char *buf, int max)
{
  int i, cc;
  char c;

  for(i=0; i+1 < max; ){
    cc = read(0, &c, 1);
    if(cc < 1)
      break;
    buf[i++] = c;
    if(c == '\n' || c == '\r')
      break;
  }
  buf[i] = '\0';
  return buf;
}

int atoi(const char *s)
{
  int n;

  n = 0;
  while('0' <= *s && *s <= '9')
    n = n*10 + *s++ - '0';
  return n;
}

void* memmove(void *vdst, const void *vsrc, int n)
{
  char *dst;
  const char *src;

  dst = vdst;
  src = vsrc;
  if (src > dst) {
    while(n-- > 0)
      *dst++ = *src++;
  } else {
    dst += n;
    src += n;
    while(n-- > 0)
      *--dst = *--src;
  }
  return vdst;
}

int memcmp(const void *s1, const void *s2, uint n)
{
  const char *p1 = s1, *p2 = s2;
  while (n-- > 0) {
    if (*p1 != *p2) {
      return *p1 - *p2;
    }
    p1++;
    p2++;
  }
  return 0;
}

void* memcpy(void *dst, const void *src, uint n)
{
  return memmove(dst, src, n);
}

int stat(const char *n, struct stat *st)
{
  int fd;
  int r;

  fd = open(n, O_RDONLY);
  if(fd < 0)
    return -1;
  r = fstat(fd, st);
  close(fd);
  return r;
}

char* strchr(const char *s, char c)
{
  for(; *s; s++)
    if(*s == c)
      return (char*)s;
  return 0;
}

char* sbrk(int n)
{
  return sys_sbrk(n, SBRK_EAGER);
}

char* sbrklazy(int n)
{
  return sys_sbrk(n, SBRK_LAZY);
}

// Handle signal sig with handler (or SIG_DFL, SIG_IGN); returns the
// previous handler, or SIG_ERR. The kernel calls handler(sig) with a
// return address of sigreturn_tramp, which resumes the interrupted
// code.
sighandler_t
signal(int sig, sighandler_t handler)
{
  return sys_signal(sig, handler, sigreturn_tramp);
}

// Seconds since the epoch.
uint64
time(void)
{
  struct timespec ts;
  if(clock_gettime(CLOCK_REALTIME, &ts) < 0)
    return 0;
  return ts.tv_sec;
}

// Days since 1970-01-01 of the given date (proleptic Gregorian
// calendar; m is 1..12).
static long
days_from_civil(long y, int m, int d)
{
  y -= m <= 2;
  long era = (y >= 0 ? y : y - 399) / 400;
  long yoe = y - era * 400;
  long doy = (153 * (m > 2 ? m - 3 : m + 9) + 2) / 5 + d - 1;
  long doe = yoe * 365 + yoe / 4 - yoe / 100 + doy;
  return era * 146097 + doe - 719468;
}

// Break t, seconds since the epoch, into UTC date and time in *tm.
struct tm *
gmtime(uint64 t, struct tm *tm)
{
  long days = t / 86400, secs = t % 86400;

  tm->tm_hour = secs / 3600;
  tm->tm_min = secs / 60 % 60;
  tm->tm_sec = secs % 60;
  tm->tm_wday = (days + 4) % 7;   // 1970-01-01 was a Thursday

  // civil_from_days, from Howard Hinnant's date algorithms
  long z = days + 719468;
  long era = (z >= 0 ? z : z - 146096) / 146097;
  long doe = z - era * 146097;
  long yoe = (doe - doe / 1460 + doe / 36524 - doe / 146096) / 365;
  long doy = doe - (365 * yoe + yoe / 4 - yoe / 100);
  long mp = (5 * doy + 2) / 153;
  int d = doy - (153 * mp + 2) / 5 + 1;
  int m = mp < 10 ? mp + 3 : mp - 9;
  long y = yoe + era * 400 + (m <= 2);

  tm->tm_year = y - 1900;
  tm->tm_mon = m - 1;
  tm->tm_mday = d;
  tm->tm_yday = days - days_from_civil(y, 1, 1);
  return tm;
}
