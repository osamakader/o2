#ifndef CONTEXT_H
#define CONTEXT_H

#include "types.h"

// Callee-saved registers for kernel context switches (swtch.S).
struct context {
    uint64 ra;
    uint64 sp;
    uint64 s0;
    uint64 s1;
    uint64 s2;
    uint64 s3;
    uint64 s4;
    uint64 s5;
    uint64 s6;
    uint64 s7;
    uint64 s8;
    uint64 s9;
    uint64 s10;
    uint64 s11;
};

// Set up c so that swtch() to it starts a kernel thread running
// fn(arg) on the stack ending at sp. kthread_trampoline (swtch.S)
// finds arg in s0 and fn in s1.
static inline void context_kthread(struct context *c, void (*fn)(void *), void *arg, uint64 sp)
{
    extern void kthread_trampoline(void);

    c->ra = (uint64)kthread_trampoline;
    c->sp = sp;
    c->s0 = (uint64)arg;
    c->s1 = (uint64)fn;
}

#endif
