#include "defs.h"
#include "memlayout.h"
#include "param.h"
#include "proc.h"
#include "console.h"
#include "cpu.h"
#include "vm.h"
#include "mmu.h"

// Entry point for a freshly created kernel thread (arch/arm64/swtch.S).
// swtch() restores x19/x20 as callee-saved registers, so kthread_create()
// stashes arg/fn there to hand them off once the thread starts running.
extern void kthread_trampoline(void);

// Fixed-size process table. BSS is zeroed at boot (arch/arm64/entry.S),
// and UNUSED == 0, so every slot starts out free with no extra init.
static struct proc ptable[NPROC];

// The proc currently running on this core, i.e. the one that owns the
// stack scheduler() last swtch()'d into. yield() uses this instead of a
// hardcoded proc so any thread made via kthread_create() can call it.
static struct proc *current;

// Context of scheduler(): yield() swtch()es back here.
// Becomes per-CPU once secondary cores run threads.
static struct context sched_ctx;

struct proc *myproc(void) {
    return current;
}

// Allocate a page for each ptable slot's kernel stack and map it at
// KSTACK(slot) in the kernel page table. Called once from kvmmake().
void proc_mapstacks(pagetable_t pagetable) {
    for (int i = 0; i < NPROC; i++) {
        char *pa = kalloc();
        if (!pa)
            panic("proc_mapstacks");
        if (mappages(pagetable, KSTACK(i), PGSIZE, V2P(pa), PTE_KERN_DATA) != 0)
            panic("proc_mapstacks: map");
        ptable[i].kstack = KSTACK(i);
    }
}

static struct proc *allocproc(void) {
    for (int i = 0; i < NPROC; i++) {
        struct proc *p = &ptable[i];
        if (p->state == UNUSED) {
            memset(&p->ctx, 0, sizeof(p->ctx));
            p->state = USED;
            return p;
        }
    }
    return 0;
}

struct proc *kthread_create(void (*fn)(void *), void *arg) {
    struct proc *p = allocproc();
    if (!p)
        return 0;

    p->ctx.ra = (uint64)kthread_trampoline;
    p->ctx.sp = p->kstack + PGSIZE;
    p->ctx.x19 = (uint64)arg;
    p->ctx.x20 = (uint64)fn;
    p->state = RUNNABLE;
    return p;
}

// Called by the currently running thread to give up the CPU.
// swtch() runs with IRQs masked so an interrupt can't re-enter the
// scheduler half way through; the caller's IRQ state is restored when
// this thread is resumed.
void yield() {
    struct proc *p = current;
    int intena = intr_get();

    intr_off();
    p->state = RUNNABLE;
    swtch(&p->ctx, &sched_ctx);
    if (intena)
        intr_on();
}

// Called by the currently running thread to terminate itself. The
// thread is still on its own kstack here, so the slot can't be reused
// yet; mark it ZOMBIE and let scheduler() reclaim it once we are off
// that stack.
void kthread_exit(void) {
    struct proc *p = current;
    intr_off();
    p->state = ZOMBIE;
    swtch(&p->ctx, &sched_ctx);
    panic("zombie thread resumed");
}

// Per-CPU scheduler loop, never returns. Round-robins over ptable,
// switching to each RUNNABLE thread until it yields or exits, and
// reclaims exited threads once it is back on its own stack.
void scheduler(void) {
    for (;;) {
        int ran = 0;

        // Let pending interrupts (e.g. timer ticks) in, then walk the
        // table and switch with IRQs masked.
        intr_on();
        intr_off();

        for (int i = 0; i < NPROC; i++) {
            struct proc *p = &ptable[i];
            if (p->state != RUNNABLE)
                continue;

            p->state = RUNNING;
            current = p;
            swtch(&sched_ctx, &p->ctx);
            current = 0;
            ran = 1;

            // the kstack stays mapped for the slot's next thread
            if (p->state == ZOMBIE)
                p->state = UNUSED;
        }

        // nothing to run: sleep until the next interrupt. wfi wakes on a
        // pending IRQ even while masked; intr_on() above then takes it.
        if (!ran)
            asm volatile("wfi");
    }
}
