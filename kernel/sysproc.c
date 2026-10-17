#include "types.h"
#include "param.h"
#include "defs.h"
#include "proc.h"
#include "syscall.h"
#include "vm.h"
#include "mmu.h"
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

// sbrk(n, type): grow or shrink memory by n bytes, returning the old
// size. Growing with SBRK_LAZY only raises sz; vmfault() allocates
// each page when it is first touched.
uint64 sys_sbrk(void) {
    int n, type;
    struct proc *p = myproc();

    argint(0, &n);
    argint(1, &type);
    uint64 addr = p->sz;

    if (type == SBRK_EAGER || n < 0) {
        if (growproc(n) < 0)
            return -1;
    } else {
        if (addr + n < addr || addr + n > USERTOP)
            return -1;
        p->sz += n;
    }
    return addr;
}

// pause(n): sleep for n ticks; returns -1 early if the process is
// killed. (xv6 renamed sleep to pause.)
uint64 sys_pause(void) {
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
