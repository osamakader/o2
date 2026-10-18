#ifndef TRAP_H
#define TRAP_H

#include "types.h"
#include "cpu.h"

// Registers saved on a trap. For traps from user mode this is the
// process's trap frame page, mapped at TRAPFRAME in its page table and
// filled by uservec (trampoline.S); the kernel_* fields tell uservec
// how to enter the kernel. Kernel traps (kernelvec, trapvec.S) build
// one on the kernel stack. x[0] is unused.
struct trapframe {
    uint64 kernel_satp;   //   0 kernel page table
    uint64 kernel_sp;     //   8 top of process's kernel stack
    uint64 kernel_trap;   //  16 usertrap()
    uint64 kernel_hartid; //  24 kernel tp
    uint64 epc;           //  32 sepc
    uint64 status;        //  40 sstatus
    uint64 cause;         //  48 scause
    uint64 tval;          //  56 stval
    uint64 x[32];         //  64 registers; x[2] = sp, x[4] = tp
};

_Static_assert(sizeof(struct trapframe) == 320, "trapvec.S, trampoline.S offsets");

// System call convention: number in a7, arguments in a0-a5, result in
// a0. Generic code (kernel/syscall.c) goes through these.
#define TF_SYSNUM(tf)  ((tf)->x[17])
#define TF_ARG(tf, n)  ((tf)->x[10 + (n)])
#define TF_RET(tf)     ((tf)->x[10])

// for signal delivery: user pc, stack pointer, return address
#define TF_PC(tf)      ((tf)->epc)
#define TF_SP(tf)      ((tf)->x[2])
#define TF_RA(tf)      ((tf)->x[1])

// Copy the user-visible state from src to dst: registers and pc.
// Nothing else, so that src may come from user memory (sigreturn)
// without touching the kernel_* fields or sstatus.
static inline void tf_copy_user(struct trapframe *dst, const struct trapframe *src)
{
    for (int i = 1; i < 32; i++)
        dst->x[i] = src->x[i];
    dst->epc = src->epc;
}

// Set up tf to start a user program at pc with stack pointer sp: user
// mode (SPP = 0) with interrupts enabled after sret (SPIE).
static inline void tf_init_user(struct trapframe *tf, uint64 pc, uint64 sp)
{
    tf->epc = pc;
    tf->x[2] = sp;
    tf->status = SSTATUS_SPIE;
}

void usertrap(void) __attribute__((noreturn));
void usertrapret(void) __attribute__((noreturn));

#endif
