#include "types.h"
#include "console.h"
#include "param.h"
#include "memlayout.h"
#include "cpu.h"

__attribute__((aligned(16), section(".stack"))) char stack0[4096 * NCPU];

// Per-CPU stacks for fatal exceptions taken at EL1 (arch/arm64/
// exceptions.S), kept in SP_EL0 so a kernel stack overflow can still be
// reported.
__attribute__((aligned(16), section(".stack"))) char estack[4096 * NCPU];

static void setup_serial() {
    pl011_init(P2V(UART0), 24000000);
}

void start(void) {
    if (cpuid() == 0) {
        setup_serial();

        log_msg("CPU 0 is up!\n");
    }
}
