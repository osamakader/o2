#ifndef CONTEXT_H
#define CONTEXT_H

#include "types.h"

// Callee-saved registers for kernel context switches (swtch.S).
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

// Set up c so that swtch() to it starts a kernel thread running
// fn(arg) on the stack ending at sp. kthread_trampoline (swtch.S)
// finds arg in x19 and fn in x20.
static inline void context_kthread(struct context *c, void (*fn)(void *), void *arg, uint64 sp)
{
    extern void kthread_trampoline(void);

    c->ra = (uint64)kthread_trampoline;
    c->sp = sp;
    c->x19 = (uint64)arg;
    c->x20 = (uint64)fn;
}

#endif
