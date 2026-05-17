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

void log_uint(uint64_t x)
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
