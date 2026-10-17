#include "syscall.h"

// System call stubs: number in x8, arguments already in x0-x5,
// result comes back in x0. A cpp macro, so SYS_* expands.
#define SYSCALL(name) \
    .global name; \
    name: \
    mov x8, #SYS_##name; \
    svc #0; \
    ret

SYSCALL(fork)
SYSCALL(exit)
SYSCALL(wait)
SYSCALL(read)
SYSCALL(kill)
SYSCALL(getpid)
SYSCALL(sbrk)
SYSCALL(sleep)
SYSCALL(uptime)
SYSCALL(write)
