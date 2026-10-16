// Sleeping locks

#include "types.h"
#include "param.h"
#include "spinlock.h"
#include "sleeplock.h"
#include "proc.h"

void initsleeplock(struct sleeplock *lk, const char *name) {
    initlock(&lk->lk, "sleep lock");
    lk->name = name;
    lk->locked = 0;
    lk->owner = 0;
}

void acquiresleep(struct sleeplock *lk) {
    acquire(&lk->lk);
    while (lk->locked)
        sleep(lk, &lk->lk);
    lk->locked = 1;
    lk->owner = myproc();
    release(&lk->lk);
}

void releasesleep(struct sleeplock *lk) {
    acquire(&lk->lk);
    lk->locked = 0;
    lk->owner = 0;
    wakeup(lk);
    release(&lk->lk);
}

int holdingsleep(struct sleeplock *lk) {
    acquire(&lk->lk);
    int r = lk->locked && lk->owner == myproc();
    release(&lk->lk);
    return r;
}
