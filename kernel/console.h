#ifndef CONSOLE_H
#define CONSOLE_H

#include "uart.h"

void log_msg(const char *msg);
void log_hex(uint64_t x);
void log_uint(uint64_t x, int base);

#endif
