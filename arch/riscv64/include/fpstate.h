#ifndef FPSTATE_H
#define FPSTATE_H

#include "types.h"

// A process's FP registers: f0-f31 (D extension) and fcsr (fpu.S
// offsets).
struct fpstate {
    uint64 f[32];
    uint64 fcsr;   // 256
};

#endif
