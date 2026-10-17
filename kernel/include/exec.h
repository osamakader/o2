#ifndef EXEC_H
#define EXEC_H

#include "types.h"

struct proc;

// Where exec() reads an ELF program from. read() copies n bytes at
// offset off into dst and returns n, or -1. Only in-memory images for
// now; files will plug in here.
struct elfsrc {
    int (*read)(struct elfsrc *src, uint64 off, void *dst, uint64 n);
    const uchar *img;  // in-memory image
    uint64 len;
};

void elfsrc_mem(struct elfsrc *src, const uchar *img, uint64 len);
int exec(struct proc *p, const char *name, struct elfsrc *src, char **argv);

#endif
