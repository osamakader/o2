#include "types.h"
#include "console.h"
#include "param.h"
#include "memlayout.h"
#include "cpu.h"

__attribute__((aligned(16), section(".stack"))) char stack0[4096 * NCPU];

static void setup_serial() {
    pl011_init(P2V(UART0), 24000000);
}

void start(void) {
    if (cpuid() == 0) {
        setup_serial();

        log_msg("CPU 0 is up!\n");
    }
}
