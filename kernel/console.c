#include <stdarg.h>

#include "console.h"
#include "spinlock.h"
#include "cpu.h"

// Serializes output from different CPUs, one log_msg() at a time.
static struct spinlock cons_lock = { .name = "cons" };

// Set by panic(); from then on output bypasses cons_lock, which the
// panicking CPU may hold.
static volatile int panicking;

void log_msg(const char *msg) {
    if (panicking) {
        pl011_send(msg);
        return;
    }
    acquire(&cons_lock);
    pl011_send(msg);
    release(&cons_lock);
}

static void cons_putc(char c) {
    char s[2] = { c, '\0' };
    pl011_send(s);
}

static void printint(uint64 x, int base, int sign) {
    static const char digits[] = "0123456789abcdef";
    char buf[24];
    int i = 0;

    if (sign && (long)x < 0)
        x = -(long)x;
    else
        sign = 0;

    do {
        buf[i++] = digits[x % base];
    } while ((x /= base) != 0);

    if (sign)
        buf[i++] = '-';

    while (--i >= 0)
        cons_putc(buf[i]);
}

// Minimal kernel printf: %d %u %x %ld %lu %lx %p %s %c %%. The whole
// call holds cons_lock, so lines from different CPUs don't interleave.
void printf(const char *fmt, ...) {
    va_list ap;
    int locking = !panicking;

    if (locking)
        acquire(&cons_lock);

    va_start(ap, fmt);
    for (int i = 0; fmt[i]; i++) {
        char c = fmt[i];
        if (c != '%') {
            if (c == '\n')
                cons_putc('\r');
            cons_putc(c);
            continue;
        }

        int lng = 0;
        c = fmt[++i];
        if (c == 'l') {
            lng = 1;
            c = fmt[++i];
        }

        switch (c) {
        case 'd':
            printint(lng ? va_arg(ap, long) : va_arg(ap, int), 10, 1);
            break;
        case 'u':
            printint(lng ? va_arg(ap, uint64) : va_arg(ap, uint32), 10, 0);
            break;
        case 'x':
            printint(lng ? va_arg(ap, uint64) : va_arg(ap, uint32), 16, 0);
            break;
        case 'p':
            cons_putc('0');
            cons_putc('x');
            printint(va_arg(ap, uint64), 16, 0);
            break;
        case 's': {
            const char *s = va_arg(ap, const char *);
            pl011_send(s ? s : "(null)");
            break;
        }
        case 'c':
            cons_putc(va_arg(ap, int));
            break;
        case '%':
            cons_putc('%');
            break;
        case 0:
            i--;   // trailing '%': stop at the terminator
            break;
        default:
            // unknown conversion: print it as is
            cons_putc('%');
            cons_putc(c);
            break;
        }
    }
    va_end(ap);

    if (locking)
        release(&cons_lock);
}

void log_hex(uint64 x)
{
    log_msg("0x");
    for (int i = 60; i >= 0; i -= 4) {
        char c = (x >> i) & 0xf;
        char s[2] = {c < 10 ? c + '0' : c + 'a' - 10, '\0'};
        log_msg(s);
    }
    log_msg("\n");
}

void log_uint(uint64 x)
{
    char buf[32];
    int i = 0;
    if (x == 0) {
        log_msg("0");
        return;
    }
    while (x > 0) {
        int digit = x % 10;
        buf[i++] = '0' + digit;
        x /= 10;
    }
    buf[i] = '\0';
    while (i > 0) {
        char s[2] = { buf[--i], '\0' };
        log_msg(s);
    }
}

void panic(const char *s)
{
  intr_off();
  panicking = 1;
  log_msg("panic: ");
  log_msg(s);
  log_msg("\n");
  for(;;)
    asm volatile("wfi");
}