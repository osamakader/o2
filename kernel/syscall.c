#include "types.h"
#include "param.h"
#include "memlayout.h"
#include "defs.h"
#include "console.h"
#include "proc.h"
#include "vm.h"
#include "syscall.h"

// Fetch the uint64 at addr from the current process.
int fetchaddr(uint64 addr, uint64 *ip) {
    struct proc *p = myproc();
    // both tests needed, in case of overflow
    if (addr >= p->sz || addr + sizeof(uint64) > p->sz)
        return -1;
    if (copyin(p->pagetable, (char *)ip, addr, sizeof(*ip)) != 0)
        return -1;
    return 0;
}

// Fetch the nul-terminated string at addr from the current process.
// Returns length of string, not including nul, or -1 for error.
int fetchstr(uint64 addr, char *buf, int max) {
    struct proc *p = myproc();
    if (copyinstr(p->pagetable, buf, addr, max) < 0)
        return -1;
    return strlen(buf);
}

static uint64 argraw(int n) {
    return TF_ARG(myproc()->trapframe, n);
}

// Fetch the nth 32-bit system call argument.
void argint(int n, int *ip) {
    *ip = argraw(n);
}

// Retrieve an argument as a pointer. Doesn't check for legality, since
// copyin/copyout will do that.
void argaddr(int n, uint64 *ip) {
    *ip = argraw(n);
}

// Fetch the nth word-sized system call argument as a null-terminated
// string. Copies into buf, at most max. Returns string length if OK
// (including nul), -1 if error.
int argstr(int n, char *buf, int max) {
    uint64 addr;
    argaddr(n, &addr);
    return fetchstr(addr, buf, max);
}

// Prototypes for the functions that handle system calls.
extern uint64 sys_getpid(void);
extern uint64 sys_write(void);

// An array mapping syscall numbers from syscall.h to the function that
// handles the system call.
static uint64 (*syscalls[])(void) = {
    [SYS_getpid] sys_getpid,
    [SYS_write]  sys_write,
};

void syscall(void) {
    struct proc *p = myproc();
    uint64 num = TF_SYSNUM(p->trapframe);

    if (num > 0 && num < NELEM(syscalls) && syscalls[num]) {
        // Use num to lookup the system call function for num, call
        // it, and store its return value in the return register.
        TF_RET(p->trapframe) = syscalls[num]();
    } else {
        printf("%d %s: unknown sys call %ld\n", p->pid, p->name, (long)num);
        TF_RET(p->trapframe) = -1;
    }
}
