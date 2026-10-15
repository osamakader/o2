#include "defs.h"
#include "memlayout.h"
#include "proc.h"

// Entry point for a freshly created kernel thread (arch/arm64/swtch.S).
// swtch() restores x19/x20 as callee-saved registers, so kthread_create()
// stashes arg/fn there to hand them off once the thread starts running.
extern void kthread_trampoline(void);

#define NPROC 8

// Fixed-size process table. BSS is zeroed at boot (arch/arm64/entry.S),
// and UNUSED == 0, so every slot starts out free with no extra init.
static struct proc ptable[NPROC];

// The proc currently running on this core, i.e. the one that owns the
// stack sched() last swtch()'d into. yield() uses this instead of a
// hardcoded proc so any thread made via kthread_create() can call it.
static struct proc *current;

void procinit(struct proc *p) {
    memset(&p->ctx, 0, sizeof(p->ctx));
    p->state = UNUSED;
    p->kstack = 0;
}

static struct proc *allocproc(void) {
    for (int i = 0; i < NPROC; i++) {
        if (ptable[i].state == UNUSED) {
            procinit(&ptable[i]);
            ptable[i].state = USED;
            return &ptable[i];
        }
    }
    return 0;
}

struct proc *kthread_create(void (*fn)(void *), void *arg) {
    struct proc *p = allocproc();
    if (!p)
        return 0;

    p->kstack = kalloc();
    if (!p->kstack) {
        p->state = UNUSED;
        return 0;
    }

    p->ctx.ra = (uint64)kthread_trampoline;
    p->ctx.sp = (uint64)p->kstack + PGSIZE;
    p->ctx.x19 = (uint64)arg;
    p->ctx.x20 = (uint64)fn;
    p->state = RUNNABLE;
    return p;
}

// Called by the currently running thread to give up the CPU.
void yield() {
    struct proc *p = current;
    p->state = RUNNABLE;
    swtch(&p->ctx, &sched_ctx);
}

// Called by the scheduler to dispatch p.
void sched(struct proc *p) {
    p->state = RUNNING;
    current = p;
    swtch(&sched_ctx, &p->ctx);
}
