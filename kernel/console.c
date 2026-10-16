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