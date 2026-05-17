#include "console.h"

void log_msg(const char *msg) {
    pl011_send(msg);
}

void log_hex(uint64_t x)
{
    log_msg("0x");
    for (int i = 60; i >= 0; i -= 4) {
        char c = (x >> i) & 0xf;
        char s[2] = {c + '0', '\0'};
        log_msg(s);
    }
    log_msg("\n");
}
