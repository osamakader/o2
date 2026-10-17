#include "types.h"

// number of elements in fixed-size array
#define NELEM(x) (sizeof(x)/sizeof((x)[0]))

// syscall.c
int argstr(int n, char *buf, int max);
void argint(int n, int *ip);
void argaddr(int n, uint64 *ip);
int fetchstr(uint64 addr, char *buf, int max);
int fetchaddr(uint64 addr, uint64 *ip);
void syscall(void);
// string.c
void* memset(void *dst, int c, uint n);
int memcmp(const void *v1, const void *v2, uint n);
void* memmove(void *dst, const void *src, uint n);
void* memcpy(void *dst, const void *src, uint n);
int strncmp(const char *p, const char *q, uint n);
char* strncpy(char *s, const char *t, int n);
char* safestrcpy(char *s, const char *t, int n);
int strlen(const char *s);

// kalloc.c
void kinit(void);
void *kalloc(void);
void kfree(void *pa);
uint64 kfreepages(void);

// selftest.c
void selftest(void);
