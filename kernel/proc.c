#include "defs.h"
#include "memlayout.h"
#include "param.h"
#include "proc.h"
#include "console.h"
#include "cpu.h"
#include "vm.h"
#include "mmu.h"
#include "spinlock.h"
#include "exec.h"
#include "procinfo.h"
#include "fpu.h"

// Fixed-size process table. BSS is zeroed at boot (arch/<arch>/entry.S),
// and UNUSED == 0, so every slot starts out free.
static struct proc ptable[NPROC];

static int nextpid = 1;
static struct spinlock pid_lock = { .name = "nextpid" };

static int allocpid(void) {
    acquire(&pid_lock);
    int pid = nextpid++;
    release(&pid_lock);
    return pid;
}

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
        ptable[i].trapframe = trapframe_alloc(KSTACK(i));
        // ASID 0 is the kernel's. A slot's ASID is flushed whenever
        // its address space is freed or replaced, so the next one to
        // use it starts with no stale translations.
        ptable[i].asid = i + 1;
    }
}

// Top of p's kernel stack for kernel code: below the trapframe if the
// arch keeps it on the kernel stack.
static uint64 kstack_top(struct proc *p) {
    uint64 tf = (uint64)p->trapframe;
    if (tf >= p->kstack && tf < p->kstack + PGSIZE)
        return tf;
    return p->kstack + PGSIZE;
}

// Find an UNUSED slot and claim it with a fresh pid. Returns with
// p->lock held.
static struct proc *allocproc(void) {
    for (int i = 0; i < NPROC; i++) {
        struct proc *p = &ptable[i];
        acquire(&p->lock);
        if (p->state == UNUSED) {
            p->pid = allocpid();
            p->state = USED;
            memset(&p->ctx, 0, sizeof(p->ctx));
            return p;
        }
        release(&p->lock);
    }
    return 0;
}

// the first user process; orphans are reparented to it.
struct proc *initproc;

// helps ensure that wakeups of wait()ing parents are not lost. helps
// obey the memory model when using p->parent. must be acquired before
// any p->lock.
static struct spinlock wait_lock = { .name = "wait_lock" };

// Assembly in exceptions.S: switch to tf and return to user mode.
extern void userret(struct trapframe *tf) __attribute__((noreturn));

// A fork child's (or the first process's) very first scheduling by
// scheduler() will swtch to forkret.
static void forkret(void) {
    static int first = 1;
    struct proc *p = myproc();

    // Still holding p->lock from scheduler.
    release(&p->lock);

    if (first) {
        // File system initialization must be run in the context of a
        // regular process (e.g., because it calls sleep), and thus
        // cannot be run from main().
        fsinit(ROOTDEV);
        first = 0;
        // ensure other cores see first = 0.
        __sync_synchronize();
#ifdef SELFTEST
        selftest_fs();
#endif
    }

    userret(p->trapframe);
}

// Free a proc structure and the data hanging from it, including user
// pages. p->lock must be held. The kernel stack stays mapped for the
// slot's next user.
static void freeproc(struct proc *p) {
    if (p->pagetable) {
        proc_freepagetable(p->pagetable, p->sz);
        asid_flush(p->asid);
    }
    p->pagetable = 0;
    p->sz = 0;
    p->pid = 0;
    p->parent = 0;
    p->name[0] = 0;
    p->chan = 0;
    p->killed = 0;
    p->xstate = 0;
    p->state = UNUSED;
}

struct proc *kthread_create(void (*fn)(void *), void *arg) {
    struct proc *p = allocproc();
    if (!p)
        return 0;

    context_kthread(&p->ctx, fn, arg, p->kstack + PGSIZE);
    safestrcpy(p->name, "kthread", sizeof(p->name));
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

    fp_switch_out(p);

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

                // Drop the user address space while still holding
                // p->lock: once released, the parent's wait() may free
                // the page table.
                if (p->pagetable)
                    kvmswitch();

                // Exited kernel threads have no parent waiting for
                // them; reclaim the slot now that we're off its stack.
                if (p->state == ZOMBIE && p->pagetable == 0)
                    freeproc(p);
            }
            release(&p->lock);
        }

        // nothing to run: sleep until the next interrupt. wfi wakes on a
        // pending IRQ even while masked; intr_on() above then takes it.
        if (!found)
            wfi();
    }
}

// Set up the first user process, running the embedded initcode
// program, which execs /init.
void userinit(void) {
    extern uchar initcode[], initcode_end[];
    struct elfsrc src;
    char *argv[] = { "initcode", 0 };

    struct proc *p = allocproc();
    if (p == 0)
        panic("userinit: allocproc");
    initproc = p;

    memset(p->trapframe, 0, sizeof(*p->trapframe));
    elfsrc_mem(&src, initcode, initcode_end - initcode);
    if (exec(p, "initcode", &src, argv) < 0)
        panic("userinit: exec");

    p->cwd = namei("/");

    // forkret runs at the top of the kernel stack.
    p->ctx.ra = (uint64)forkret;
    p->ctx.sp = kstack_top(p);

    p->state = RUNNABLE;
    release(&p->lock);
}

// Grow or shrink user memory by n bytes. Return 0 on success, -1 on
// failure.
int growproc(int n) {
    struct proc *p = myproc();
    uint64 sz = p->sz;

    if (n > 0) {
        if (sz + n > USERTOP)
            return -1;
        if ((sz = uvmalloc(p->pagetable, sz, sz + n, PERM_W)) == 0)
            return -1;
    } else if (n < 0) {
        // shrinking below 0 wraps sz + n around: uvmdealloc() then
        // leaves the size unchanged, as xv6 does.
        sz = uvmdealloc(p->pagetable, sz, sz + n);
    }
    p->sz = sz;
    // Drop stale translations for unmapped pages; on riscv64 new
    // mappings need a fence too.
    if (n != 0)
        asid_flush(p->asid);
    return 0;
}

// Create a new process, copying the parent. Sets up the child's
// kernel stack to return as if from the fork() system call, with 0 in
// the return register.
int fork(void) {
    struct proc *p = myproc();

    // Allocate process.
    struct proc *np = allocproc();
    if (np == 0)
        return -1;

    // Copy user memory from parent to child.
    if ((np->pagetable = proc_pagetable(np)) == 0 ||
        uvmcopy(p->pagetable, np->pagetable, p->sz) < 0) {
        freeproc(np);
        release(&np->lock);
        return -1;
    }
    np->sz = p->sz;

    // copy saved user registers.
    *(np->trapframe) = *(p->trapframe);

    fp_fork(p, np);

    // Cause fork to return 0 in the child.
    TF_RET(np->trapframe) = 0;

    // increment reference counts on open file descriptors.
    for (int i = 0; i < NOFILE; i++)
        if (p->ofile[i])
            np->ofile[i] = filedup(p->ofile[i]);
    np->cwd = idup(p->cwd);

    safestrcpy(np->name, p->name, sizeof(p->name));

    // The child starts in forkret, on its kernel stack below the
    // trapframe.
    np->ctx.ra = (uint64)forkret;
    np->ctx.sp = kstack_top(np);

    int pid = np->pid;

    release(&np->lock);

    acquire(&wait_lock);
    np->parent = p;
    release(&wait_lock);

    acquire(&np->lock);
    np->state = RUNNABLE;
    release(&np->lock);

    return pid;
}

// Pass p's abandoned children to init.
// Caller must hold wait_lock.
static void reparent(struct proc *p) {
    for (int i = 0; i < NPROC; i++) {
        struct proc *pp = &ptable[i];
        if (pp->parent == p) {
            pp->parent = initproc;
            wakeup(initproc);
        }
    }
}

// Exit the current user process. Does not return. An exited process
// remains in the zombie state until its parent calls wait().
void exit(int status) {
    struct proc *p = myproc();

    if (p == initproc)
        panic("init exiting");

    // Close all open files.
    for (int fd = 0; fd < NOFILE; fd++) {
        if (p->ofile[fd]) {
            struct file *f = p->ofile[fd];
            fileclose(f);
            p->ofile[fd] = 0;
        }
    }

    begin_op();
    iput(p->cwd);
    end_op();
    p->cwd = 0;

    acquire(&wait_lock);

    // Give any children to init.
    reparent(p);

    // Parent might be sleeping in wait().
    wakeup(p->parent);

    acquire(&p->lock);

    p->xstate = status;
    p->state = ZOMBIE;

    release(&wait_lock);

    // Jump into the scheduler, never to return.
    sched();
    panic("zombie exit");
}

// Wait for a child process to exit and return its pid. Return -1 if
// this process has no children. If addr is not 0, copy the child's
// exit status there.
int wait(uint64 addr) {
    struct proc *p = myproc();

    acquire(&wait_lock);

    for (;;) {
        // Scan through table looking for exited children.
        int havekids = 0;
        for (int i = 0; i < NPROC; i++) {
            struct proc *pp = &ptable[i];
            if (pp->parent != p)
                continue;

            // make sure the child isn't still in exit() or swtch().
            acquire(&pp->lock);

            havekids = 1;
            if (pp->state == ZOMBIE) {
                // Found one.
                int pid = pp->pid;
                if (addr != 0 && copyout(p->pagetable, addr, (char *)&pp->xstate,
                                         sizeof(pp->xstate)) < 0) {
                    release(&pp->lock);
                    release(&wait_lock);
                    return -1;
                }
                freeproc(pp);
                release(&pp->lock);
                release(&wait_lock);
                return pid;
            }
            release(&pp->lock);
        }

        // No point waiting if we don't have any children.
        if (!havekids || killed(p)) {
            release(&wait_lock);
            return -1;
        }

        // Wait for a child to exit.
        sleep(p, &wait_lock);
    }
}

// Kill the process with the given pid. The victim won't exit until it
// tries to return to user space (see usertrap()).
int kill(int pid) {
    for (int i = 0; i < NPROC; i++) {
        struct proc *p = &ptable[i];
        acquire(&p->lock);
        if (p->pid == pid && p->pagetable != 0) {
            p->killed = 1;
            if (p->state == SLEEPING) {
                // Wake process from sleep().
                p->state = RUNNABLE;
            }
            release(&p->lock);
            return 0;
        }
        release(&p->lock);
    }
    return -1;
}

// The parent's pid; 0 for init, which has none.
int getppid(void) {
    acquire(&wait_lock);
    struct proc *parent = myproc()->parent;
    int pid = parent ? parent->pid : 0;
    release(&wait_lock);
    return pid;
}

void setkilled(struct proc *p) {
    acquire(&p->lock);
    p->killed = 1;
    release(&p->lock);
}

int killed(struct proc *p) {
    acquire(&p->lock);
    int k = p->killed;
    release(&p->lock);
    return k;
}

// Copy to either a user address, or kernel address, depending on
// user_dst. Returns 0 on success, -1 on error.
int either_copyout(int user_dst, uint64 dst, void *src, uint64 len) {
    if (user_dst)
        return copyout(myproc()->pagetable, dst, src, len);
    memmove((char *)dst, src, len);
    return 0;
}

// Copy from either a user address, or kernel address, depending on
// user_src. Returns 0 on success, -1 on error.
int either_copyin(void *dst, int user_src, uint64 src, uint64 len) {
    if (user_src)
        return copyin(myproc()->pagetable, dst, src, len);
    memmove(dst, (char *)src, len);
    return 0;
}

// Print a process listing to the console, for debugging. Runs when a
// user types ^P on the console. No lock, to avoid wedging a stuck
// machine further.
void procdump(void) {
    static char *states[] = {
        [UNUSED]   "unused",
        [USED]     "used",
        [SLEEPING] "sleep ",
        [RUNNABLE] "runble",
        [RUNNING]  "run   ",
        [ZOMBIE]   "zombie",
    };

    printf("\n");
    for (int i = 0; i < NPROC; i++) {
        struct proc *p = &ptable[i];
        if (p->state == UNUSED)
            continue;
        char *state = "???";
        if (p->state >= 0 && p->state < NELEM(states) && states[p->state])
            state = states[p->state];
        printf("%d %s %s", p->pid, state, p->name);
        if (p->state == SLEEPING)
            printf(" chan=%p", p->chan);
        printf("\n");
    }
}

// Copy a struct procinfo for each process and kernel thread, up to
// max, to the user array at addr. Returns the number copied, or -1 if
// addr isn't writable.
int getprocs(uint64 addr, int max) {
    static const int codes[] = {
        [USED]     PS_USED,
        [SLEEPING] PS_SLEEPING,
        [RUNNABLE] PS_RUNNABLE,
        [RUNNING]  PS_RUNNING,
        [ZOMBIE]   PS_ZOMBIE,
    };
    struct proc *me = myproc();
    int n = 0;

    for (int i = 0; i < NPROC && n < max; i++) {
        struct proc *p = &ptable[i];
        struct procinfo pi;

        memset(&pi, 0, sizeof(pi));

        // wait_lock keeps p->parent (and the parent itself) stable.
        acquire(&wait_lock);
        acquire(&p->lock);
        if (p->state == UNUSED) {
            release(&p->lock);
            release(&wait_lock);
            continue;
        }
        pi.pid = p->pid;
        pi.ppid = p->parent ? p->parent->pid : 0;
        pi.state = codes[p->state];
        pi.kthread = p->pagetable == 0;
        pi.sz = p->sz;
        safestrcpy(pi.name, p->name, sizeof(pi.name));
        release(&p->lock);
        release(&wait_lock);

        if (copyout(me->pagetable, addr + n * sizeof(pi), (char *)&pi, sizeof(pi)) < 0)
            return -1;
        n++;
    }
    return n;
}
