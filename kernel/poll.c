// poll(fds, nfds, timeout): wait until one of the files can be read or
// written without waiting, or timeout milliseconds pass (-1: forever).
//
// Pollers sleep on one channel. Whatever may make a file ready (pipes,
// the console, the input device, sockets) calls pollwakeup(), which
// counts a generation and wakes them all to look again; a poller only
// sleeps if the generation it looked at is still the current one, so
// it can't miss a wakeup. Those with a timeout are woken every tick too.

#include "types.h"
#include "param.h"
#include "memlayout.h"
#include "defs.h"
#include "spinlock.h"
#include "sleeplock.h"
#include "proc.h"
#include "fs.h"
#include "file.h"
#include "poll.h"
#include "timer.h"

static struct spinlock polllock = { .name = "poll" };
static uint64 pollgen;
static int ntimed;    // pollers sleeping with a timeout

void pollwakeup(void)
{
    acquire(&polllock);
    pollgen++;
    wakeup(&pollgen);
    release(&polllock);
}

// Every tick (clockintr()).
void poll_tick(void)
{
    if (__atomic_load_n(&ntimed, __ATOMIC_RELAXED)) {
        acquire(&polllock);
        wakeup(&pollgen);
        release(&polllock);
    }
}

// What is so of f: POLLIN, POLLOUT, POLLHUP.
static int filepoll(struct file *f)
{
    switch (f->type) {
    case FD_PIPE:
        return pipepoll(f->pipe, f->writable);
    case FD_SOCK:
        return sockpoll(f->sock);
    case FD_DEVICE:
        if (f->major >= 0 && f->major < NDEV && devsw[f->major].poll)
            return devsw[f->major].poll() | (f->writable ? POLLOUT : 0);
        return (f->readable ? POLLIN : 0) | (f->writable ? POLLOUT : 0);
    default:
        // files never make you wait
        return (f->readable ? POLLIN : 0) | (f->writable ? POLLOUT : 0);
    }
}

#define NPOLL (2 * NOFILE)

uint64 sys_poll(void)
{
    struct proc *p = myproc();
    struct pollfd fds[NPOLL];
    uint64 addr;
    int nfds, timeout;

    argaddr(0, &addr);
    argint(1, &nfds);
    argint(2, &timeout);
    if (nfds < 0 || nfds > NPOLL ||
        copyin(p->pagetable, (char *)fds, addr, nfds * sizeof(fds[0])) < 0)
        return -1;
    uint64 t0 = ticks, wait = timeout < 0 ? 0 : ((uint64)timeout * HZ + 999) / 1000;

    for (;;) {
        acquire(&polllock);
        uint64 gen = pollgen;
        release(&polllock);

        int n = 0;
        for (int i = 0; i < nfds; i++) {
            struct file *f;
            int fd = fds[i].fd;
            fds[i].revents = 0;
            if (fd < 0)
                continue;
            if (fd >= NOFILE || (f = p->ofile[fd]) == 0)
                fds[i].revents = POLLNVAL;
            else
                fds[i].revents = filepoll(f) & (fds[i].events | POLLHUP);
            if (fds[i].revents)
                n++;
        }
        if (n > 0 || (timeout >= 0 && ticks - t0 >= wait)) {
            if (copyout(p->pagetable, addr, (char *)fds, nfds * sizeof(fds[0])) < 0)
                return -1;
            return n;
        }
        if (interrupted(p))
            return -1;

        acquire(&polllock);
        if (pollgen == gen) {
            if (timeout >= 0)
                ntimed++;
            sleep(&pollgen, &polllock);
            if (timeout >= 0)
                ntimed--;
        }
        release(&polllock);
    }
}
