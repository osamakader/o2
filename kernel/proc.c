#include "defs.h"
#include "memlayout.h"
#include "param.h"
#include "proc.h"
#include "console.h"
#include "cpu.h"
#include "vm.h"
#include "mmu.h"
#include "spinlock.h"

// Entry point for a freshly created kernel thread (arch/arm64/swtch.S).
// swtch() restores x19/x20 as callee-saved registers, so kthread_create()
// stashes arg/fn there to hand them off once the thread starts running.
extern void kthread_trampoline(void);

// Fixed-size process table. BSS is zeroed at boot (arch/arm64/entry.S),
// and UNUSED == 0, so every slot starts out free.
static struct proc ptable[NPROC];

void procinit(void) {
    for (int i = 0; i < NPROC; i++)
        initlock(&ptable[i].lock, "proc");
}

struct cpu cpus[NCPU];

// This core's struct cpu. Call with interrupts off.
struct cpu *mycpu(void) {
    return &cpus[cpuid()];
}

// The thread running on this core, or 0 in the scheduler.
struct proc *myproc(void) {
    push_off();
    struct proc *p = mycpu()->proc;
    pop_off();
    return p;
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

// Find an UNUSED slot and claim it. Returns with p->lock held.
static struct proc *allocproc(void) {
    for (int i = 0; i < NPROC; i++) {
        struct proc *p = &ptable[i];
        acquire(&p->lock);
        if (p->state == UNUSED) {
            memset(&p->ctx, 0, sizeof(p->ctx));
            p->state = USED;
            return p;
        }
        release(&p->lock);
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
    release(&p->lock);
    return p;
}

// First C code a new thread runs, from kthread_trampoline. scheduler()
// switched here holding p->lock; drop it. The intena that release()
// restores belongs to whatever ran on this CPU before (possibly a
// thread preempted from an IRQ, with interrupts off), so turn them on
// explicitly: kernel threads start interruptible.
void kthread_start(void) {
    release(&myproc()->lock);
    intr_on();
}

// Switch to the scheduler. Must hold only p->lock and have already
// changed p->state. Saves and restores intena because intena is a
// property of this thread, not of the CPU it happens to run on.
static void sched(void) {
    struct proc *p = myproc();

    if (!holding(&p->lock))
        panic("sched p->lock");
    if (mycpu()->noff != 1)
        panic("sched locks");
    if (p->state == RUNNING)
        panic("sched running");
    if (intr_get())
        panic("sched interruptible");

    int intena = mycpu()->intena;
    swtch(&p->ctx, &mycpu()->context);
    mycpu()->intena = intena;
}

// Called by the currently running thread to give up the CPU.
void yield() {
    struct proc *p = myproc();

    acquire(&p->lock);
    p->state = RUNNABLE;
    sched();
    release(&p->lock);
}

// Atomically release lk and sleep on chan. Reacquires lk when
// awakened. Callers re-check their condition in a loop: a wakeup only
// means it may have changed.
void sleep(void *chan, struct spinlock *lk) {
    struct proc *p = myproc();

    // Must acquire p->lock in order to change p->state and then call
    // sched(). Once we hold p->lock, we can be guaranteed that we won't
    // miss any wakeup (wakeup locks p->lock), so it's okay to release
    // lk.
    acquire(&p->lock);
    release(lk);

    p->chan = chan;
    p->state = SLEEPING;

    sched();

    // Tidy up.
    p->chan = 0;

    // Reacquire original lock.
    release(&p->lock);
    acquire(lk);
}

// Wake up all threads sleeping on chan. Must be called without any
// p->lock; callers normally hold the lock that sleepers on chan pass to
// sleep().
void wakeup(void *chan) {
    struct proc *me = myproc();

    for (int i = 0; i < NPROC; i++) {
        struct proc *p = &ptable[i];
        if (p == me)
            continue;
        acquire(&p->lock);
        if (p->state == SLEEPING && p->chan == chan)
            p->state = RUNNABLE;
        release(&p->lock);
    }
}

// Called by the currently running thread to terminate itself. The
// thread is still on its own kstack here, so the slot can't be reused
// yet; mark it ZOMBIE and let scheduler() reclaim it once we are off
// that stack.
void kthread_exit(void) {
    struct proc *p = myproc();

    acquire(&p->lock);
    p->state = ZOMBIE;
    sched();
    panic("zombie thread resumed");
}

// Per-CPU scheduler loop, never returns. Round-robins over ptable,
// switching to each RUNNABLE thread until it yields or exits, and
// reclaims exited threads once it is back on its own stack.
// p->lock is held across swtch() in both directions: the thread
// releases it on the other side (yield(), kthread_start()), and
// re-acquires it before switching back.
void scheduler(void) {
    struct cpu *c = mycpu();

    c->proc = 0;
    for (;;) {
        // The most recent thread may have had interrupts off; enable
        // them so pending ones (e.g. timer ticks) get taken, then turn
        // them back off to avoid a race between an interrupt and wfi.
        intr_on();
        intr_off();

        int found = 0;
        for (int i = 0; i < NPROC; i++) {
            struct proc *p = &ptable[i];
            acquire(&p->lock);
            if (p->state == RUNNABLE) {
                p->state = RUNNING;
                c->proc = p;
                swtch(&c->context, &p->ctx);
                c->proc = 0;
                found = 1;

                // the kstack stays mapped for the slot's next thread
                if (p->state == ZOMBIE)
                    p->state = UNUSED;
            }
            release(&p->lock);
        }

        // nothing to run: sleep until the next interrupt. wfi wakes on a
        // pending IRQ even while masked; intr_on() above then takes it.
        if (!found)
            asm volatile("wfi");
    }
}
