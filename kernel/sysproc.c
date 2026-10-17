#include "types.h"
#include "param.h"
#include "defs.h"
#include "proc.h"
#include "spinlock.h"
#include "timer.h"

uint64 sys_getpid(void) {
    return myproc()->pid;
}

uint64 sys_exit(void) {
    int n;
    argint(0, &n);
    exit(n);
    return 0;  // not reached
}

uint64 sys_wait(void) {
    uint64 p;
    argaddr(0, &p);
    return wait(p);
}

uint64 sys_kill(void) {
    int pid;
    argint(0, &pid);
    return kill(pid);
}

uint64 sys_fork(void) {
    return fork();
}

uint64 sys_sbrk(void) {
    int n;
    argint(0, &n);
    uint64 addr = myproc()->sz;
    if (growproc(n) < 0)
        return -1;
    return addr;
}

// Sleep for n ticks; returns -1 early if the process is killed.
uint64 sys_sleep(void) {
    int n;
    argint(0, &n);
    if (n < 0)
        n = 0;

    acquire(&tickslock);
    uint64 ticks0 = ticks;
    while (ticks - ticks0 < n) {
        if (killed(myproc())) {
            release(&tickslock);
            return -1;
        }
        sleep(&ticks, &tickslock);
    }
    release(&tickslock);
    return 0;
}

// return how many clock tick interrupts have occurred since start.
uint64 sys_uptime(void) {
    acquire(&tickslock);
    uint64 xticks = ticks;
    release(&tickslock);
    return xticks;
}

// getprocs(struct procinfo *buf, int max): list processes, for ps.
uint64 sys_getprocs(void) {
    uint64 buf;
    int max;

    argaddr(0, &buf);
    argint(1, &max);
    if (max < 0)
        return -1;
    return getprocs(buf, max);
}
