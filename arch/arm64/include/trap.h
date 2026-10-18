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

// for signal delivery: user pc, stack pointer, return address
#define TF_PC(tf)      ((tf)->elr)
#define TF_SP(tf)      ((tf)->sp)
#define TF_RA(tf)      ((tf)->x[30])

#define SPSR_NZCV      (0xfUL << 28)

// Copy the user-visible state from src to dst: registers, pc, sp and
// the condition flags. Nothing else, so that src may come from user
// memory (sigreturn) without letting it change the exception level or
// unmask anything.
static inline void tf_copy_user(struct trapframe *dst, const struct trapframe *src)
{
    for (int i = 0; i < 31; i++)
        dst->x[i] = src->x[i];
    dst->elr = src->elr;
    dst->sp = src->sp;
    dst->spsr = (dst->spsr & ~SPSR_NZCV) | (src->spsr & SPSR_NZCV);
}

// Set up tf to start a user program at pc with stack pointer sp:
// EL0t with interrupts unmasked.
static inline void tf_init_user(struct trapframe *tf, uint64 pc, uint64 sp)
{
    tf->elr = pc;
    tf->sp = sp;
    tf->spsr = 0;
}

void fiq_handler(struct trapframe *tf);
void serr_handler(struct trapframe *tf);
void sync_handler(struct trapframe *tf);
void usertrap(struct trapframe *tf);
void usertrapret(struct trapframe *tf);
void usertrap_irq(struct trapframe *tf);
void handle_irq(struct trapframe *tf);
#endif