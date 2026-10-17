#ifndef TRAP_H
#define TRAP_H

#include "types.h"
#include "cpu.h"

// Registers saved on a trap (trapvec.S). x[0] is unused; for traps from
// user mode x[2] is the user sp and x[4] the user tp.
struct trapframe {
    uint64 x[32];
    uint64 epc;      // sepc
    uint64 status;   // sstatus
    uint64 cause;    // scause
    uint64 tval;     // stval
    uint64 hartid;   // kernel tp to load on the next trap from user mode
    uint64 pad;      // keep the size a multiple of 16
};

_Static_assert(sizeof(struct trapframe) == 304, "trapvec.S TF_SIZE");

// System call convention: number in a7, arguments in a0-a5, result in
// a0. Generic code (kernel/syscall.c) goes through these.
#define TF_SYSNUM(tf)  ((tf)->x[17])
#define TF_ARG(tf, n)  ((tf)->x[10 + (n)])
#define TF_RET(tf)     ((tf)->x[10])

// Set up tf to start a user program at pc with stack pointer sp: user
// mode (SPP = 0) with interrupts enabled after sret (SPIE).
static inline void tf_init_user(struct trapframe *tf, uint64 pc, uint64 sp)
{
    tf->epc = pc;
    tf->x[2] = sp;
    tf->status = SSTATUS_SPIE;
}

void usertrap(struct trapframe *tf);
void usertrapret(struct trapframe *tf);

#endif
