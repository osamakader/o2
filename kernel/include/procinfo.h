#ifndef PROCINFO_H
#define PROCINFO_H

#include "types.h"

// One process, as reported by getprocs(); shared with user space (ps).

// process states
#define PS_USED     1   // being created
#define PS_SLEEPING 2
#define PS_RUNNABLE 3
#define PS_RUNNING  4
#define PS_ZOMBIE   5

struct procinfo {
    int pid;
    int ppid;        // 0 if none (init, kernel threads)
    int state;       // PS_*
    int kthread;     // kernel thread: no user address space
    uint64 sz;       // user memory, bytes
    char name[16];
};

#endif
