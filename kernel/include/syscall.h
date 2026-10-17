#ifndef SYSCALL_H
#define SYSCALL_H

// System call numbers, shared with user space (user/usys.S).
// The registers used are the arch's (TF_SYSNUM/TF_ARG/TF_RET in trap.h,
// stubs in user/<arch>/usys.S).
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
