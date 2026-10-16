#ifndef PROC_H
#define PROC_H

#include "types.h"
#include "vm.h"

enum procstate { UNUSED, USED, RUNNABLE, RUNNING, ZOMBIE };

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
    uint64 kstack;   // VA of the kernel stack, KSTACK(slot)
};

void swtch(struct context *old, struct context *new);

void proc_mapstacks(pagetable_t pagetable);
struct proc *kthread_create(void (*fn)(void *), void *arg);
void yield(void);
void kthread_exit(void) __attribute__((noreturn));
void scheduler(void) __attribute__((noreturn));
struct proc *myproc(void);

#endif