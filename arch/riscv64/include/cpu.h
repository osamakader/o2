#ifndef CPU_H
#define CPU_H

#include "types.h"

// ELF e_machine of programs this kernel runs
#define ELF_MACHINE 243   // EM_RISCV

#define SSTATUS_SIE  (1UL << 1)   // supervisor interrupt enable
#define SSTATUS_SPIE (1UL << 5)   // previous SIE, restored by sret
#define SSTATUS_SPP  (1UL << 8)   // previous mode: 1 = S, 0 = U

#define SIE_SSIE (1UL << 1)   // software
#define SIE_STIE (1UL << 5)   // timer
#define SIE_SEIE (1UL << 9)   // external

#define csr_read(csr) ({ uint64 __v; asm volatile("csrr %0, " #csr : "=r"(__v)); __v; })
#define csr_write(csr, v) asm volatile("csrw " #csr ", %0" : : "r"((uint64)(v)))
#define csr_set(csr, v) asm volatile("csrs " #csr ", %0" : : "r"((uint64)(v)))
#define csr_clear(csr, v) asm volatile("csrc " #csr ", %0" : : "r"((uint64)(v)))

// This hart's id, kept in tp while in the kernel (entry.S, trapvec.S).
// Only stable while interrupts are off; a thread may migrate otherwise.
static inline int cpuid(void) {
    uint64 x;
    asm volatile("mv %0, tp" : "=r"(x));
    return x;
}

// Wait for an interrupt. Wakes on a pending interrupt even while
// sstatus.SIE is clear.
static inline void wfi(void) {
    asm volatile("wfi");
}

static inline void intr_on(void) {
    asm volatile("csrs sstatus, %0" : : "r"(SSTATUS_SIE) : "memory");
}

static inline void intr_off(void) {
    asm volatile("csrc sstatus, %0" : : "r"(SSTATUS_SIE) : "memory");
}

// Are interrupts enabled?
static inline int intr_get(void) {
    return (csr_read(sstatus) & SSTATUS_SIE) != 0;
}

#endif
