#ifndef CONSOLE_H
#define CONSOLE_H

#include "uart.h"

void log_msg(const char *msg);
void log_hex(uint64 x);
void log_uint(uint64 x);
void panic(const char *s);

#endif
