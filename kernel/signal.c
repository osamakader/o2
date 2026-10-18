// Signal handlers: calling them in user mode, and returning from them.
// Sending signals and default actions are in proc.c (kill(),
// usersignals()).
//
// To call a handler, the kernel saves the process's user registers and
// FP state in a signal frame on its user stack and makes it return to
// user mode in handler(sig), with the stack pointer at the frame and
// the return address set to the restorer registered with signal() (the
// user library's sigreturn_tramp). The restorer's sigreturn system
// call restores the registers from the frame, resuming the code the
// signal interrupted.

#include "types.h"
#include "param.h"
#include "defs.h"
#include "console.h"
#include "spinlock.h"
#include "proc.h"
#include "vm.h"
#include "fpu.h"

struct sigframe {
    struct trapframe tf;   // only what tf_copy_user() copies
    struct fpstate fp;
};

// Set up p, the current process, to run handler(sig) when it returns
// to user mode. If the frame doesn't fit on its stack, p exits.
void sigdeliver(struct proc *p, int sig, uint64 handler) {
    struct trapframe *tf = p->trapframe;
    struct sigframe frame;

    memset(&frame, 0, sizeof(frame));
    tf_copy_user(&frame.tf, tf);
    fp_sync(p);
    frame.fp = p->fp;

    // below the interrupted code's stack, 16-byte aligned for the ABIs
    uint64 sp = (TF_SP(tf) - sizeof(frame)) & ~15UL;
    if (sp > TF_SP(tf) || copyout(p->pagetable, sp, (char *)&frame, sizeof(frame)) < 0) {
        printf("pid %d %s: no room for a signal frame\n", p->pid, p->name);
        exit(-1);
    }

    TF_PC(tf) = handler;
    TF_SP(tf) = sp;
    TF_RA(tf) = p->sigrestorer;
    TF_ARG(tf, 0) = sig;
}

// signal(sig, handler, restorer): handle sig with handler, a user
// function or SIG_DFL/SIG_IGN, returning through restorer. Returns the
// previous handler, or SIG_ERR (-1). SIGKILL can't be handled.
uint64 sys_signal(void) {
    struct proc *p = myproc();
    int sig;
    uint64 handler, restorer;

    argint(0, &sig);
    argaddr(1, &handler);
    argaddr(2, &restorer);
    if (sig <= 0 || sig >= NSIG || sig == SIGKILL)
        return (uint64)SIG_ERR;

    acquire(&p->lock);
    uint64 old = p->sighandler[sig];
    p->sighandler[sig] = handler;
    if (handler == (uint64)SIG_IGN)
        p->sigpending &= ~(1U << sig);   // dropped, as if never sent
    else if (handler != (uint64)SIG_DFL)
        p->sigrestorer = restorer;
    release(&p->lock);
    return old;
}

// sigreturn(), from the restorer when a handler returns: the stack
// pointer is back at the signal frame. Restore what it holds. The frame
// is in user memory, so the program may have changed it; tf_copy_user()
// takes only state user code may set anyway.
uint64 sys_sigreturn(void) {
    struct proc *p = myproc();
    struct sigframe frame;

    if (copyin(p->pagetable, (char *)&frame, TF_SP(p->trapframe), sizeof(frame)) < 0) {
        setkilled(p);
        return -1;
    }
    tf_copy_user(p->trapframe, &frame.tf);
    p->fp = frame.fp;
    fp_reload(p);

    // syscall() puts the return value in the return register: keep the
    // one the frame restored
    return TF_RET(p->trapframe);
}
