#ifndef TRAP_H
#define TRAP_H

#include "types.h"

struct trapframe {
    uint64 x[31];
    uint64 elr;
    uint64 spsr;
    uint64 esr;
    uint64 far;
    uint64 sp;      // user SP_EL0, saved on traps from EL0
};

_Static_assert(sizeof(struct trapframe) == 288, "exceptions.S TF_SIZE");

// System call convention: number in x8, arguments in x0-x5, result in
// x0. Generic code (kernel/syscall.c) goes through these.
#define TF_SYSNUM(tf)  ((tf)->x[8])
#define TF_ARG(tf, n)  ((tf)->x[n])
#define TF_RET(tf)     ((tf)->x[0])

void fiq_handler(struct trapframe *tf);
void serr_handler(struct trapframe *tf);
void sync_handler(struct trapframe *tf);
void usertrap(struct trapframe *tf);
void usertrapret(struct trapframe *tf);
void usertrap_irq(struct trapframe *tf);
#endif