#ifndef CPU_H
#define CPU_H

#include "types.h"

// This core's index: MPIDR_EL1 Aff0, which qemu virt numbers 0..ncpu-1.
// Only stable while interrupts are off; a thread may migrate otherwise.
static inline int cpuid(void) {
    uint64 mpidr;
    asm volatile("mrs %0, mpidr_el1" : "=r"(mpidr));
    return mpidr & 0xff;
}

// IRQ mask helpers for the current core (PSTATE.I).

static inline void intr_on(void) {
    asm volatile("msr daifclr, #2" ::: "memory");
}

static inline void intr_off(void) {
    asm volatile("msr daifset, #2" ::: "memory");
}

// Are IRQs currently unmasked?
static inline int intr_get(void) {
    uint64 daif;
    asm volatile("mrs %0, daif" : "=r"(daif));
    return (daif & (1 << 7)) == 0;
}

#endif
