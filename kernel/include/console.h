#ifndef CONSOLE_H
#define CONSOLE_H

#include <uart.h>

void log_msg(const char *msg);
void consoleintr(int c);
int consoleread(char *dst, int n);
int consolewrite(uint64 src, int n);
void printf(const char *fmt, ...) __attribute__((format(printf, 1, 2)));
void panic(const char *fmt, ...) __attribute__((noreturn, format(printf, 1, 2)));

#endif
