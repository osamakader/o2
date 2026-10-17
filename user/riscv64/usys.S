#include "syscall.h"

// System call stubs: number in a7, arguments already in a0-a5,
// result comes back in a0. A cpp macro, so SYS_* expands.
#define SYSCALL(name) \
    .global name; \
    name: \
    li a7, SYS_##name; \
    ecall; \
    ret

SYSCALL(fork)
SYSCALL(exit)
SYSCALL(wait)
SYSCALL(read)
SYSCALL(kill)
SYSCALL(exec)
SYSCALL(getpid)
SYSCALL(sbrk)
SYSCALL(sleep)
SYSCALL(uptime)
SYSCALL(write)
SYSCALL(pipe)
SYSCALL(fstat)
SYSCALL(chdir)
SYSCALL(dup)
SYSCALL(open)
SYSCALL(mknod)
SYSCALL(unlink)
SYSCALL(link)
SYSCALL(mkdir)
SYSCALL(close)
