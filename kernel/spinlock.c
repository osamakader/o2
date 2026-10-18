// Mutual exclusion spin locks.

#include "types.h"
#include "param.h"
#include "spinlock.h"
#include "proc.h"
#include "console.h"
#include "cpu.h"
#include "defs.h"
#include "vm.h"
#include "lockstat.h"

// Statistics per lock name, counted per CPU so that counting doesn't
// make CPUs contend for the counters: slot i counts the locks named
// statname[i].
#define NLOCKSTAT 64
static const char *statname[NLOCKSTAT];
static int nstat;
static struct {
    uint64 acquire, contended, spin;
} stats[NCPU][NLOCKSTAT];

// The slot for locks named name, + 1: the existing one, or a new one
// (the last slot takes all names once they run out).
static int stat_slot(const char *name) {
    int n = __atomic_load_n(&nstat, __ATOMIC_ACQUIRE);
    for (int i = 0; i < n && i < NLOCKSTAT; i++) {
        const char *s = __atomic_load_n(&statname[i], __ATOMIC_ACQUIRE);
        if (s && (s == name || strncmp(s, name, 16) == 0))
            return i + 1;
    }
    int i = __atomic_fetch_add(&nstat, 1, __ATOMIC_ACQ_REL);
    if (i >= NLOCKSTAT)
        return NLOCKSTAT;
    __atomic_store_n(&statname[i], name, __ATOMIC_RELEASE);
    return i + 1;
}

void initlock(struct spinlock *lk, const char *name) {
    lk->name = name;
    lk->locked = 0;
    lk->cpu = 0;
    lk->stat = 0;
}

// Acquire the lock. Loops (spins) until the lock is acquired.
// Interrupts stay off until the matching release(), so an interrupt
// handler on this core can't deadlock trying to take the same lock.
void acquire(struct spinlock *lk) {
    push_off();
    if (holding(lk))
        panic("acquire");

    // ldaxr/stxr loop: the acquire ordering keeps the critical
    // section's loads and stores after the lock is taken.
    uint64 spins = 0;
    while (__atomic_exchange_n(&lk->locked, 1, __ATOMIC_ACQUIRE) != 0)
        spins++;

    // Record info about lock acquisition for holding() and debugging.
    lk->cpu = mycpu();

    if (lk->stat == 0)
        lk->stat = stat_slot(lk->name);
    int c = cpuid(), i = lk->stat - 1;
    stats[c][i].acquire++;
    if (spins) {
        stats[c][i].contended++;
        stats[c][i].spin += spins;
    }
}

// Release the lock.
void release(struct spinlock *lk) {
    if (!holding(lk))
        panic("release");

    lk->cpu = 0;

    // stlr: the critical section's stores are visible before the
    // lock reads as free.
    __atomic_store_n(&lk->locked, 0, __ATOMIC_RELEASE);

    pop_off();
}

// Check whether this cpu is holding the lock.
// Interrupts must be off.
int holding(struct spinlock *lk) {
    return lk->locked && lk->cpu == mycpu();
}

// push_off/pop_off are like intr_off()/intr_on() except that they are
// matched: it takes two pop_off()s to undo two push_off()s. Also, if
// interrupts are initially off, then push_off, pop_off leaves them off.
void push_off(void) {
    int old = intr_get();

    intr_off();
    if (mycpu()->noff == 0)
        mycpu()->intena = old;
    mycpu()->noff += 1;
}

void pop_off(void) {
    struct cpu *c = mycpu();
    if (intr_get())
        panic("pop_off - interruptible");
    if (c->noff < 1)
        panic("pop_off");
    c->noff -= 1;
    if (c->noff == 0 && c->intena)
        intr_on();
}

// lockstat(buf, max): statistics for up to max lock names (struct
// lockstat), summed over the CPUs. Returns how many.
uint64 sys_lockstat(void) {
    uint64 addr;
    int max;

    argaddr(0, &addr);
    argint(1, &max);
    int n = __atomic_load_n(&nstat, __ATOMIC_ACQUIRE);
    if (n > NLOCKSTAT)
        n = NLOCKSTAT;
    int i;
    for (i = 0; i < n && i < max; i++) {
        struct lockstat ls;
        memset(&ls, 0, sizeof(ls));
        const char *name = __atomic_load_n(&statname[i], __ATOMIC_ACQUIRE);
        safestrcpy(ls.name, i == NLOCKSTAT - 1 ? "(others)" : name ? name : "?", sizeof(ls.name));
        for (int c = 0; c < NCPU; c++) {
            ls.acquire += stats[c][i].acquire;
            ls.contended += stats[c][i].contended;
            ls.spin += stats[c][i].spin;
        }
        if (copyout(myproc()->pagetable, addr + i * sizeof(ls), (char *)&ls, sizeof(ls)) < 0)
            return -1;
    }
    return i;
}
