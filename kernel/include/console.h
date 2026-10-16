#ifndef CONSOLE_H
#define CONSOLE_H

#include <uart.h>

void log_msg(const char *msg);
void log_hex(uint64 x);
void log_uint(uint64 x);
void printf(const char *fmt, ...) __attribute__((format(printf, 1, 2)));
void panic(const char *s) __attribute__((noreturn));

#endif
