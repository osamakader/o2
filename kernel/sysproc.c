#include "types.h"
#include "param.h"
#include "defs.h"
#include "proc.h"

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
