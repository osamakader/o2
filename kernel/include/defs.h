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

// bio.c
struct buf;
void binit(void);
struct buf *bread(uint dev, uint blockno);
void brelse(struct buf *b);
void bwrite(struct buf *b);
void bpin(struct buf *b);
void bunpin(struct buf *b);

// virtio_disk.c
void virtio_disk_init(void);
int virtio_disk_present(void);
struct buf;
void virtio_disk_rw(struct buf *b, int write);

// arch: start the other CPUs; each enters main(0)
void start_cpus(void);

// selftest.c
void selftest(void);
