#ifndef PROC_H
#define PROC_H

#include "types.h"

enum procstate { UNUSED, RUNNABLE, RUNNING };

struct context {
    uint64 ra;   // x30
    uint64 sp;
    uint64 x19;
    uint64 x20;
    uint64 x21;
    uint64 x22;
    uint64 x23;
    uint64 x24;
    uint64 x25;
    uint64 x26;
    uint64 x27;
    uint64 x28;
    uint64 x29;
};

struct proc {
    enum procstate state;
    struct context ctx;
    void *kstack;
};

void swtch(struct context *old, struct context *new);

#endif