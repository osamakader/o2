#ifndef FPSTATE_H
#define FPSTATE_H

#include "types.h"

// A process's FP/SIMD registers: q0-q31, FPSR, FPCR (fpu.S offsets).
struct fpstate {
    uint64 q[64];
    uint32 fpsr;   // 512
    uint32 fpcr;   // 516
} __attribute__((aligned(16)));

#endif
