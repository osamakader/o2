#ifndef SPINLOCK_H
#define SPINLOCK_H

#include "types.h"

struct cpu;

// Mutual exclusion lock.
struct spinlock {
    uint locked;       // is the lock held?

    // for debugging:
    const char *name;  // name of lock.
    struct cpu *cpu;   // the cpu holding the lock.
};

void initlock(struct spinlock *lk, const char *name);
void acquire(struct spinlock *lk);
void release(struct spinlock *lk);
int holding(struct spinlock *lk);
void push_off(void);
void pop_off(void);

#endif
