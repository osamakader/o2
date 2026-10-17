#include "types.h"
#include "param.h"

// Boot stacks, one page per CPU, set up by arch/<arch>/entry.S. Each
// CPU's scheduler() keeps running on its boot stack.
__attribute__((aligned(16), section(".stack"))) char stack0[4096 * NCPU];

// Per-CPU stacks for fatal exceptions taken in the kernel, so a kernel
// stack overflow can still be reported.
__attribute__((aligned(16), section(".stack"))) char estack[4096 * NCPU];
