#include <stdint.h>
#include "uart.h"

#define NCPU 8
__attribute__((aligned(16), section(".stack"))) char stack0[4096 * NCPU];

static uint32_t get_cpu_id() {
    uint64_t x;
    asm volatile("mrs %0, MPIDR_EL1" : "=r" (x));
    return x & 0xff;
}

void start(void) {
    uint32_t cpu_id = get_cpu_id();
    struct pl011 serial;

    if (cpu_id == 0) {
        pl011_setup(
            &serial, 
            /* base_address = */0x9000000, 
            /* base_clock = */24000000);

        pl011_send(&serial, "Hello, World from CPU 0!\n");
    }
    else {
        pl011_send(&serial, "Hello, World from other CPU!\n");
    }
}
