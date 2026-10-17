#ifndef PROC_H
#define PROC_H

#include "types.h"
#include "vm.h"
#include "param.h"
#include "spinlock.h"
#include "trap.h"

enum procstate { UNUSED, USED, SLEEPING, RUNNABLE, RUNNING, ZOMBIE };

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
    struct spinlock lock;

    // p->lock must be held when using these:
    enum procstate state;
    void *chan;      // if non-zero, sleeping on chan
    int killed;      // if non-zero, have been killed
    int xstate;      // exit status to be returned to parent's wait
    int pid;         // process ID

    // wait_lock must be held when using this:
    struct proc *parent;

    // these are private to the process, so p->lock need not be held.
    struct context ctx;
    uint64 kstack;                // VA of the kernel stack, KSTACK(slot)
    uint64 sz;                    // size of process memory (bytes)
    pagetable_t pagetable;        // user page table; 0 for kernel threads
    struct trapframe *trapframe;  // user registers, at the top of kstack
    char name[16];                // process name (debugging)
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

void procinit(void);
void userinit(void);
int fork(void);
void exit(int status) __attribute__((noreturn));
int wait(uint64 addr);
int kill(int pid);
void setkilled(struct proc *p);
int killed(struct proc *p);
void proc_mapstacks(pagetable_t pagetable);
struct proc *kthread_create(void (*fn)(void *), void *arg);
void yield(void);
void sleep(void *chan, struct spinlock *lk);
void wakeup(void *chan);
void kthread_exit(void) __attribute__((noreturn));
void scheduler(void) __attribute__((noreturn));
struct proc *myproc(void);

#endif