#include <stdarg.h>

#include "types.h"
#include "user.h"

static char digits[] = "0123456789ABCDEF";

static void putc(int fd, char c)
{
  write(fd, &c, 1);
}

// Write s (n characters) padded to width: on the right if left, else on
// the left with pad.
static void putpadded(int fd, const char *s, int n, int width, int left, char pad)
{
  if(!left)
    for(; width > n; width--)
      putc(fd, pad);
  for(int i = 0; i < n; i++)
    putc(fd, s[i]);
  for(; width > n; width--)
    putc(fd, ' ');
}

static void printint(int fd, long long xx, int base, int sgn, int width, int left, int zero)
{
  char buf[24];
  int i, neg;
  unsigned long long x;

  neg = 0;
  if(sgn && xx < 0){
    neg = 1;
    x = -xx;
  } else {
    x = xx;
  }

  i = sizeof(buf);
  do{
    buf[--i] = digits[x % base];
  }while((x /= base) != 0);
  if(neg && zero && !left){
    // the sign goes before the zeros
    putc(fd, '-');
    width--;
  } else if(neg){
    buf[--i] = '-';
  }
  putpadded(fd, buf + i, sizeof(buf) - i, width, left, zero ? '0' : ' ');
}

static void printptr(int fd, uint64 x) {
  int i;
  putc(fd, '0');
  putc(fd, 'x');
  for (i = 0; i < (sizeof(uint64) * 2); i++, x <<= 4)
    putc(fd, digits[x >> (sizeof(uint64) * 8 - 4)]);
}

// Print to the given fd. Understands %d, %u, %x, %p, %s, %c and %%,
// with an l modifier for d/u/x, and a width, optionally with a - flag
// (pad on the right) or a 0 flag (pad numbers with zeros).
static void vprintf(int fd, const char *fmt, va_list ap)
{
  char *s;

  for(int i = 0; fmt[i]; i++){
    int c = fmt[i] & 0xff;
    if(c != '%'){
      putc(fd, c);
      continue;
    }
    int left = 0, zero = 0, width = 0, lng = 0;
    for(;; i++){
      if(fmt[i+1] == '-')
        left = 1;
      else if(fmt[i+1] == '0')
        zero = 1;
      else
        break;
    }
    while(fmt[i+1] >= '0' && fmt[i+1] <= '9')
      width = width * 10 + (fmt[++i] - '0');
    while(fmt[i+1] == 'l'){
      lng = 1;
      i++;
    }
    c = fmt[++i] & 0xff;
    if(c == 'd'){
      printint(fd, lng ? va_arg(ap, long) : va_arg(ap, int), 10, 1, width, left, zero);
    } else if(c == 'u'){
      printint(fd, lng ? va_arg(ap, uint64) : va_arg(ap, uint32), 10, 0, width, left, zero);
    } else if(c == 'x'){
      printint(fd, lng ? va_arg(ap, uint64) : va_arg(ap, uint32), 16, 0, width, left, zero);
    } else if(c == 'p'){
      printptr(fd, va_arg(ap, uint64));
    } else if(c == 's'){
      if((s = va_arg(ap, char*)) == 0)
        s = "(null)";
      putpadded(fd, s, strlen(s), width, left, ' ');
    } else if(c == 'c'){
      char ch = va_arg(ap, uint32);
      putpadded(fd, &ch, 1, width, left, ' ');
    } else if(c == '%'){
      putc(fd, '%');
    } else if(c == 0){
      break;
    } else {
      // Unknown % sequence.  Print it to draw attention.
      putc(fd, '%');
      putc(fd, c);
    }
  }
}

void fprintf(int fd, const char *fmt, ...)
{
  va_list ap;

  va_start(ap, fmt);
  vprintf(fd, fmt, ap);
  va_end(ap);
}

void printf(const char *fmt, ...)
{
  va_list ap;

  va_start(ap, fmt);
  vprintf(1, fmt, ap);
  va_end(ap);
}
