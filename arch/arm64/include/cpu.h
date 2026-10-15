#ifndef CPU_H
#define CPU_H

#include "types.h"

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
