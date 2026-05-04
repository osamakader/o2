#include <stdint.h>
#include "uart.h"

#define NCPU 8
__attribute__((aligned(16), section(".stack"))) char stack0[4096 * NCPU];

void start(void) {

    struct pl011 serial;

    pl011_setup(
        &serial, 
        /* base_address = */0x9000000, 
        /* base_clock = */24000000);

    pl011_send(&serial, "Hello, World\n");

    while (1) {
        asm volatile("wfi");
    }
}
