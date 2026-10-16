#ifndef PROC_H
#define PROC_H

#include "types.h"
#include "vm.h"
#include "param.h"

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

// Per-CPU state.
struct cpu {
    struct proc *proc;        // thread running on this cpu, or 0
    struct context context;   // swtch() here to enter scheduler()
    int noff;                 // depth of push_off() nesting
    int intena;               // were interrupts enabled before push_off()?
};

extern struct cpu cpus[NCPU];

void swtch(struct context *old, struct context *new);

struct cpu *mycpu(void);

void proc_mapstacks(pagetable_t pagetable);
struct proc *kthread_create(void (*fn)(void *), void *arg);
void yield(void);
void kthread_exit(void) __attribute__((noreturn));
void scheduler(void) __attribute__((noreturn));
struct proc *myproc(void);

#endif