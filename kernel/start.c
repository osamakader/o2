#include <stdint.h>
#include "uart.h"

#define NCPU 8
__attribute__((aligned(16), section(".stack"))) char stack0[4096 * NCPU];

int get_cpu_id() {
    uint64_t x;
    asm volatile("mrs %0, MPIDR_EL1" : "=r" (x));
    return x & 0xff;
}

void start(void) {
    int cpu_id = get_cpu_id();
    struct pl011 serial;
    pl011_setup(
        &serial, 
        /* base_address = */0x9000000, 
        /* base_clock = */24000000);
    if (cpu_id == 0)
        pl011_send(&serial, "Hello, World from CPU 0!\n");
    else
        pl011_send(&serial, "Hello, World from other CPU!\n");

    while (1) {
        asm volatile("wfi");
    }
}
