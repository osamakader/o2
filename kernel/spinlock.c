// Mutual exclusion spin locks.

#include "types.h"
#include "param.h"
#include "spinlock.h"
#include "proc.h"
#include "console.h"
#include "cpu.h"

void initlock(struct spinlock *lk, const char *name) {
    lk->name = name;
    lk->locked = 0;
    lk->cpu = 0;
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
    while (__atomic_exchange_n(&lk->locked, 1, __ATOMIC_ACQUIRE) != 0)
        ;

    // Record info about lock acquisition for holding() and debugging.
    lk->cpu = mycpu();
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
