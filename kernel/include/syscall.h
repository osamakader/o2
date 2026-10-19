#ifndef SYSCALL_H
#define SYSCALL_H

// System call numbers, shared with user space (user/usys.S).
// The registers used are the arch's (TF_SYSNUM/TF_ARG/TF_RET in trap.h,
// stubs in user/<arch>/usys.S).
#define SYS_fork    1
#define SYS_exit    2
#define SYS_wait    3
#define SYS_pipe    4
#define SYS_read    5
#define SYS_kill    6
#define SYS_exec    7
#define SYS_fstat   8
#define SYS_chdir   9
#define SYS_dup    10
#define SYS_getpid 11
#define SYS_sbrk   12
#define SYS_pause  13
#define SYS_uptime 14
#define SYS_open   15
#define SYS_write  16
#define SYS_mknod  17
#define SYS_unlink 18
#define SYS_link   19
#define SYS_mkdir  20
#define SYS_close  21
#define SYS_sync   22
#define SYS_getprocs 23   // o2 extension
#define SYS_klog   24     // o2 extension
#define SYS_getppid 25
#define SYS_dup2    26
#define SYS_lseek   27
#define SYS_waitpid 28
#define SYS_signal  29
#define SYS_sigreturn 30
#define SYS_clock_gettime 31
#define SYS_ioctl   32
#define SYS_mmap    33
#define SYS_munmap  34
#define SYS_symlink 35
#define SYS_readlink 36
#define SYS_lstat   37
#define SYS_lockstat 38
#define SYS_socket  39
#define SYS_bind    40
#define SYS_connect 41
#define SYS_sendto  42
#define SYS_recvfrom 43
#define SYS_setsockopt 44
#define SYS_fcntl   45
#define SYS_memfd   46

// sbrk(n, type): allocate now, or only on first touch (page fault)
#define SBRK_EAGER 1
#define SBRK_LAZY  2

// waitpid() options: return 0 instead of waiting if no child has exited
#define WNOHANG    1

#endif
