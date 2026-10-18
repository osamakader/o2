#ifndef CPU_H
#define CPU_H

#include "types.h"

// ELF e_machine of programs this kernel runs
#define ELF_MACHINE 183   // EM_AARCH64

// This core's index: MPIDR_EL1 Aff0, which qemu virt numbers 0..ncpu-1.
// Only stable while interrupts are off; a thread may migrate otherwise.
// This CPU's index: MPIDR's Aff0 + Aff1, one of which is 0 on the
// machines o2 knows (see entry.S).
static inline int cpuid(void) {
    uint64 mpidr;
    asm volatile("mrs %0, mpidr_el1" : "=r"(mpidr));
    return (mpidr & 0xff) + ((mpidr >> 8) & 0xff);
}

// Wait for an interrupt (low-power idle). Wakes on a pending IRQ even
// while IRQs are masked.
static inline void wfi(void) {
    asm volatile("wfi");
}

// Frame records (AAPCS64): x29 points at {previous x29, return address}.
#define FRAME_FP_SLOT(fp) ((fp) + 0)
#define FRAME_RA_SLOT(fp) ((fp) + 8)

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
