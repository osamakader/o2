#include "console.h"

void log_msg(const char *msg) {
    pl011_send(msg);
}

void log_hex(uint64_t x)
{
    log_msg("0x");
    for (int i = 60; i >= 0; i -= 4) {
        char c = (x >> i) & 0xf;
        char s[2] = {c < 10 ? c + '0' : c + 'a' - 10, '\0'};
        log_msg(s);
    }
    log_msg("\n");
}

void log_uint(uint64_t x, int base)
{
    char buf[32];
    int i = 0;
    if (x == 0) {
        log_msg("0");
        return;
    }
    while (x > 0) {
        int digit = x % base;
        buf[i++] = digit < 10 ? '0' + digit : 'a' + digit - 10;
        x /= base;
    }
    while (i > 0)
        log_msg(&buf[--i]);
}
