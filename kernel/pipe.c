// Pipes, from xv6.

#include "types.h"
#include "param.h"
#include "defs.h"
#include "console.h"
#include "spinlock.h"
#include "sleeplock.h"
#include "proc.h"
#include "vm.h"
#include "fs.h"
#include "file.h"
#include "poll.h"

#define PIPESIZE 2048   // the struct fits in a page

struct pipe {
    struct spinlock lock;
    char data[PIPESIZE];
    uint nread;     // number of bytes read
    uint nwrite;    // number of bytes written
    int readopen;   // read fd is still open
    int writeopen;  // write fd is still open
};

int pipealloc(struct file **f0, struct file **f1)
{
    struct pipe *pi = 0;

    *f0 = *f1 = 0;
    if ((*f0 = filealloc()) == 0 || (*f1 = filealloc()) == 0)
        goto bad;
    if ((pi = (struct pipe *)kalloc()) == 0)
        goto bad;
    pi->readopen = 1;
    pi->writeopen = 1;
    pi->nwrite = 0;
    pi->nread = 0;
    initlock(&pi->lock, "pipe");
    (*f0)->type = FD_PIPE;
    (*f0)->readable = 1;
    (*f0)->writable = 0;
    (*f0)->pipe = pi;
    (*f1)->type = FD_PIPE;
    (*f1)->readable = 0;
    (*f1)->writable = 1;
    (*f1)->pipe = pi;
    return 0;

bad:
    if (pi)
        kfree((char *)pi);
    if (*f0)
        fileclose(*f0);
    if (*f1)
        fileclose(*f1);
    return -1;
}

void pipeclose(struct pipe *pi, int writable)
{
    acquire(&pi->lock);
    if (writable) {
        pi->writeopen = 0;
        wakeup(&pi->nread);
        pollwakeup();
    } else {
        pi->readopen = 0;
        wakeup(&pi->nwrite);
        pollwakeup();
    }
    if (pi->readopen == 0 && pi->writeopen == 0) {
        release(&pi->lock);
        kfree((char *)pi);
    } else {
        release(&pi->lock);
    }
}

// Writes of up to PIPESIZE bytes are atomic, like POSIX's PIPE_BUF:
// they wait for room for all of it, so readers see none of it or all.
// With nonblock, a write returns -1 instead of waiting, or as much as
// it wrote of a longer one.
int pipewrite(struct pipe *pi, uint64 addr, int n, int nonblock)
{
    int i = 0;
    struct proc *pr = myproc();

    acquire(&pi->lock);
    while (i < n) {
        if (pi->readopen == 0 || interrupted(pr)) {
            release(&pi->lock);
            return -1;
        }
        int need = i == 0 && n <= PIPESIZE ? n : 1;
        if (pi->nwrite + need > pi->nread + PIPESIZE) { //DOC: pipewrite-full
            if (nonblock) {
                wakeup(&pi->nread);
                release(&pi->lock);
                return i ? i : -1;
            }
            wakeup(&pi->nread);
            sleep(&pi->nwrite, &pi->lock);
        } else {
            char ch;
            if (copyin(pr->pagetable, &ch, addr + i, 1) == -1)
                break;
            pi->data[pi->nwrite++ % PIPESIZE] = ch;
            i++;
        }
    }
    wakeup(&pi->nread);
    pollwakeup();
    release(&pi->lock);

    return i;
}

int piperead(struct pipe *pi, uint64 addr, int n, int nonblock)
{
    int i;
    struct proc *pr = myproc();
    char ch;

    acquire(&pi->lock);
    while (pi->nread == pi->nwrite && pi->writeopen) {  //DOC: pipe-empty
        if (interrupted(pr) || nonblock) {
            release(&pi->lock);
            return -1;
        }
        sleep(&pi->nread, &pi->lock); //DOC: piperead-sleep
    }
    for (i = 0; i < n; i++) {  //DOC: piperead-copy
        if (pi->nread == pi->nwrite)
            break;
        ch = pi->data[pi->nread++ % PIPESIZE];
        if (copyout(pr->pagetable, addr + i, &ch, 1) == -1)
            break;
    }
    wakeup(&pi->nwrite);  //DOC: piperead-wakeup
    pollwakeup();
    release(&pi->lock);
    return i;
}

// poll(): what is so of the pipe's read end, or with writable its write
// end. Writable when PIPESIZE/4 bytes fit, like Linux's PIPE_BUF.
int pipepoll(struct pipe *pi, int writable)
{
    int r = 0;
    acquire(&pi->lock);
    if (writable) {
        if (!pi->readopen)
            r = POLLHUP | POLLOUT;   // a write fails at once
        else if (pi->nwrite - pi->nread <= PIPESIZE - PIPESIZE / 4)
            r = POLLOUT;
    } else {
        if (pi->nread != pi->nwrite)
            r = POLLIN;
        if (!pi->writeopen)
            r |= POLLIN | POLLHUP;   // read() returns 0
    }
    release(&pi->lock);
    return r;
}
