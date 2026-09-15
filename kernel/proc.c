#include "defs.h"
#include "memlayout.h"
#include "proc.h"

// Entry point for a freshly created kernel thread (arch/arm64/swtch.S).
// swtch() restores x19/x20 as callee-saved registers, so kthread_create()
// stashes arg/fn there to hand them off once the thread starts running.
extern void kthread_trampoline(void);

void procinit(struct proc *p) {
    memset(&p->ctx, 0, sizeof(p->ctx));
    p->state = UNUSED;
    p->kstack = 0;
}

void kthread_create(void (*fn)(void *), void *arg) {
    struct proc *p = kalloc();
    if (!p)
        return;
    procinit(p);

    p->kstack = kalloc();
    if (!p->kstack)
        return;

    p->ctx.ra = (uint64)kthread_trampoline;
    p->ctx.sp = (uint64)p->kstack + PGSIZE;
    p->ctx.x19 = (uint64)arg;
    p->ctx.x20 = (uint64)fn;
    p->state = RUNNABLE;
}

void yield() {
    struct proc *p = &proc0;
    p->state = RUNNABLE;
    swtch(&proc0.ctx, &sched_ctx);
}

void sched() {
    struct proc *p = &proc0;
    p->state = RUNNING;
    swtch(&proc0.ctx, &sched_ctx);
}
