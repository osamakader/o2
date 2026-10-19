#include "syscall.h"

// System call stubs: number in x8, arguments already in x0-x5,
// result comes back in x0. A cpp macro, so SYS_* expands.
#define SYSCALL_AS(sym, name) \
    .global sym; \
    sym: \
    mov x8, #SYS_##name; \
    svc #0; \
    ret
#define SYSCALL(name) SYSCALL_AS(name, name)

SYSCALL(fork)
SYSCALL(exit)
SYSCALL(wait)
SYSCALL(read)
SYSCALL(kill)
SYSCALL(exec)
SYSCALL(getpid)
SYSCALL_AS(sys_sbrk, sbrk)   // sbrk()/sbrklazy() in ulib.c
SYSCALL(pause)
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
SYSCALL(sync)
SYSCALL(getprocs)
SYSCALL(klog)
SYSCALL(getppid)
SYSCALL(dup2)
SYSCALL(lseek)
SYSCALL(waitpid)
SYSCALL_AS(sys_signal, signal)       // signal() in ulib.c
SYSCALL_AS(sigreturn_tramp, sigreturn)  // where handlers return to
SYSCALL(clock_gettime)
SYSCALL(ioctl)
SYSCALL(mmap)
SYSCALL(munmap)
SYSCALL(symlink)
SYSCALL(readlink)
SYSCALL(lstat)
SYSCALL(lockstat)
SYSCALL(socket)
SYSCALL(bind)
SYSCALL(connect)
SYSCALL(sendto)
SYSCALL(recvfrom)
SYSCALL(setsockopt)
SYSCALL(fcntl)
SYSCALL(memfd)
