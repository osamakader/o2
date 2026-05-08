#include <stdint.h>
#include "console.h"

#define NCPU 8
__attribute__((aligned(16), section(".stack"))) char stack0[4096 * NCPU];

static uint32_t get_cpu_id() {
    uint64_t x;
    asm volatile("mrs %0, MPIDR_EL1" : "=r" (x));
    return x & 0xff;
}

static void setup_serial() {
    pl011_init(0x9000000, 24000000);
}

void start(void) {
    uint32_t cpu_id = get_cpu_id();

    if (cpu_id == 0) {
        setup_serial();

        log_msg("CPU 0 is up!\n");
    }
    else {
        log_msg("Secondary CPU is up!\n");
    }
}
