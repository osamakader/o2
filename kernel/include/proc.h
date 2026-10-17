#ifndef PROC_H
#define PROC_H

#include "types.h"
#include "vm.h"
#include "param.h"
#include "spinlock.h"
#include "trap.h"
#include "context.h"

enum procstate { UNUSED, USED, SLEEPING, RUNNABLE, RUNNING, ZOMBIE };


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
    struct file *ofile[NOFILE];   // open files
    struct inode *cwd;            // current directory
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
int growproc(int n);
int fork(void);
void exit(int status) __attribute__((noreturn));
int wait(uint64 addr);
int kill(int pid);
void setkilled(struct proc *p);
int killed(struct proc *p);
void procdump(void);
int either_copyout(int user_dst, uint64 dst, void *src, uint64 len);
int either_copyin(void *dst, int user_src, uint64 src, uint64 len);
void proc_mapstacks(pagetable_t pagetable);
struct proc *kthread_create(void (*fn)(void *), void *arg);
void yield(void);
void sleep(void *chan, struct spinlock *lk);
void wakeup(void *chan);
void kthread_exit(void) __attribute__((noreturn));
void scheduler(void) __attribute__((noreturn));
struct proc *myproc(void);

#endif