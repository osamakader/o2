#include <stdarg.h>

#include "console.h"
#include "spinlock.h"
#include "cpu.h"

// Serializes output from different CPUs, one log_msg()/printf() at a
// time.
static struct spinlock cons_lock = { .name = "cons" };

// -1 until panic(); then the id of the panicking CPU. That CPU prints
// without cons_lock (it may hold it); any other CPU that tries to print
// parks instead, so the panic message comes out intact.
static int panic_cpu = -1;

static void __attribute__((noreturn)) park(void) {
    for (;;)
        asm volatile("wfi");
}

// Start a piece of console output. Returns whether cons_lock was taken.
static int cons_begin(void) {
    int pc = __atomic_load_n(&panic_cpu, __ATOMIC_ACQUIRE);

    if (pc < 0) {
        acquire(&cons_lock);
        return 1;
    }
    intr_off();
    if (pc != cpuid())
        park();
    return 0;
}

static void cons_end(int locked) {
    if (locked)
        release(&cons_lock);
}

void log_msg(const char *msg) {
    int locked = cons_begin();
    pl011_send(msg);
    cons_end(locked);
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

static void vprintf(const char *fmt, va_list ap) {
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
}

// Minimal kernel printf: %d %u %x %ld %lu %lx %p %s %c %%. The whole
// call holds cons_lock, so lines from different CPUs don't interleave.
void printf(const char *fmt, ...) {
    va_list ap;
    int locked = cons_begin();

    va_start(ap, fmt);
    vprintf(fmt, ap);
    va_end(ap);

    cons_end(locked);
}

// Print a printf-style message and stop this CPU. The first CPU to
// panic owns the console from then on; a second CPU panicking at the
// same time parks silently. A nested panic on the owning CPU still
// prints.
void panic(const char *fmt, ...)
{
  va_list ap;
  int expected = -1;

  intr_off();
  if (!__atomic_compare_exchange_n(&panic_cpu, &expected, cpuid(), 0,
                                   __ATOMIC_ACQ_REL, __ATOMIC_ACQUIRE) &&
      expected != cpuid())
    park();

  // Take cons_lock for good, so a printf() already running on another
  // CPU finishes its line first and a racing one that slipped past
  // cons_begin() spins in acquire() instead of interleaving. Bounded,
  // in case that CPU is stuck with the lock held. Skipped if this CPU
  // panicked while printing.
  if (!holding(&cons_lock))
    for (long n = 0; n < 100000000; n++)
      if (__atomic_exchange_n(&cons_lock.locked, 1, __ATOMIC_ACQUIRE) == 0)
        break;

  printf("panic: cpu %d: ", cpuid());
  va_start(ap, fmt);
  vprintf(fmt, ap);
  va_end(ap);
  printf("\n");
  park();
}