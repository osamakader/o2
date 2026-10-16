#ifndef SLEEPLOCK_H
#define SLEEPLOCK_H

#include "types.h"
#include "spinlock.h"

struct proc;

// Long-term lock for threads: waiters sleep instead of spinning, and
// the holder may sleep (e.g. wait for disk I/O) while holding it.
struct sleeplock {
    uint locked;          // is the lock held?
    struct spinlock lk;   // spinlock protecting this sleep lock

    // for debugging:
    const char *name;     // name of lock.
    struct proc *owner;   // thread holding the lock
};

void initsleeplock(struct sleeplock *lk, const char *name);
void acquiresleep(struct sleeplock *lk);
void releasesleep(struct sleeplock *lk);
int holdingsleep(struct sleeplock *lk);

#endif
