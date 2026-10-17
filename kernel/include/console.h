#ifndef CONSOLE_H
#define CONSOLE_H

#include "types.h"

void log_msg(const char *msg);
void consoleintr(int c);
int consoleread(int user_dst, uint64 dst, int n);
int consolewrite(int user_src, uint64 src, int n);
void consoleinit(void);
void printf(const char *fmt, ...) __attribute__((format(printf, 1, 2)));
void panic(const char *fmt, ...) __attribute__((noreturn, format(printf, 1, 2)));

#endif
