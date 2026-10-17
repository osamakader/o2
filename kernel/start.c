#include "types.h"
#include "console.h"
#include "param.h"
#include "memlayout.h"
#include "cpu.h"
#include "uart.h"

__attribute__((aligned(16), section(".stack"))) char stack0[4096 * NCPU];

// Per-CPU stacks for fatal exceptions taken at EL1 (arch/arm64/
// exceptions.S), kept in SP_EL0 so a kernel stack overflow can still be
// reported.
__attribute__((aligned(16), section(".stack"))) char estack[4096 * NCPU];

void start(void) {
    if (cpuid() == 0) {
        uartinit();

        log_msg("CPU 0 is up!\n");
    }
}
