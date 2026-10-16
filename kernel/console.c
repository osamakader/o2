#include <stdarg.h>

#include "console.h"
#include "spinlock.h"
#include "cpu.h"
#include "proc.h"

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

// pl011_send() turns '\n' into "\r\n".
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
//
// Console input, fed one character at a time by the UART interrupt
// handler. Line discipline as in xv6:
//   backspace / DEL: erase a character
//   ^U: kill the line
//   ^D: end of file
// Characters are echoed; consoleread() returns whole lines.
//

#define C(x)  ((x) - '@')  // control-x
#define BACKSPACE 0x100
#define INPUT_BUF_SIZE 128

static struct {
    struct spinlock lock;
    char buf[INPUT_BUF_SIZE];
    uint r;  // read index
    uint w;  // write index: end of the last completed line
    uint e;  // edit index
} cons_in = { .lock = { .name = "cons_in" } };

// Echo one input character, erasing it from the screen for BACKSPACE.
static void echo(int c) {
    int locked = cons_begin();
    if (c == BACKSPACE) {
        cons_putc('\b');
        cons_putc(' ');
        cons_putc('\b');
    } else {
        cons_putc(c);
    }
    cons_end(locked);
}

// Called from the UART interrupt handler for each input character.
void consoleintr(int c) {
    acquire(&cons_in.lock);

    switch (c) {
    case C('U'):  // kill line
        while (cons_in.e != cons_in.w &&
               cons_in.buf[(cons_in.e - 1) % INPUT_BUF_SIZE] != '\n') {
            cons_in.e--;
            echo(BACKSPACE);
        }
        break;
    case C('H'):  // backspace
    case '\x7f':  // delete key
        if (cons_in.e != cons_in.w) {
            cons_in.e--;
            echo(BACKSPACE);
        }
        break;
    default:
        if (c != 0 && cons_in.e - cons_in.r < INPUT_BUF_SIZE) {
            c = (c == '\r') ? '\n' : c;

            echo(c);

            // store for consumption by consoleread().
            cons_in.buf[cons_in.e++ % INPUT_BUF_SIZE] = c;

            if (c == '\n' || c == C('D') || cons_in.e - cons_in.r == INPUT_BUF_SIZE) {
                // wake up consoleread() if a whole line (or end-of-file)
                // has arrived.
                cons_in.w = cons_in.e;
                wakeup(&cons_in.r);
            }
        }
        break;
    }

    release(&cons_in.lock);
}

// Read up to n bytes of input into dst, sleeping until a whole line
// (or ^D) is available. Returns the number of bytes read; 0 means
// end of file.
int consoleread(char *dst, int n) {
    int target = n;

    acquire(&cons_in.lock);
    while (n > 0) {
        // wait until the interrupt handler has put some input into
        // cons_in.buf.
        while (cons_in.r == cons_in.w)
            sleep(&cons_in.r, &cons_in.lock);

        int c = cons_in.buf[cons_in.r++ % INPUT_BUF_SIZE];

        if (c == C('D')) {  // end-of-file
            if (n < target) {
                // Save ^D for next time, to make sure caller gets a
                // 0-byte result.
                cons_in.r--;
            }
            break;
        }

        *dst++ = c;
        --n;

        if (c == '\n') {
            // a whole line has arrived, return to the caller.
            break;
        }
    }
    release(&cons_in.lock);

    return target - n;
}
