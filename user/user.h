#ifndef USER_H
#define USER_H

#include "types.h"

// system calls
int fork(void);
int exit(int) __attribute__((noreturn));
int wait(int *);
int read(int, void *, int);
int write(int, const void *, int);
int kill(int);
int getpid(void);
char *sbrk(int);
int sleep(int);
int uptime(void);

// ulib.c
char *strcpy(char *, const char *);
int strcmp(const char *, const char *);
uint strlen(const char *);
void *memset(void *, int, uint);
void *memmove(void *, const void *, int);
int memcmp(const void *, const void *, uint);
void *memcpy(void *, const void *, uint);
char *gets(char *, int max);
int atoi(const char *);

// printf.c
void fprintf(int, const char *, ...) __attribute__((format(printf, 2, 3)));
void printf(const char *, ...) __attribute__((format(printf, 1, 2)));

#endif
