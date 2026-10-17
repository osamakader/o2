#ifndef SYSCALL_H
#define SYSCALL_H

// System call numbers, shared with user space (user/usys.S).
// arm64 convention: number in x8, arguments in x0-x5, result in x0.
#define SYS_fork    1
#define SYS_exit    2
#define SYS_wait    3
#define SYS_read    5
#define SYS_kill    6
#define SYS_getpid 11
#define SYS_sbrk   12
#define SYS_sleep  13
#define SYS_uptime 14
#define SYS_write  16

#endif
