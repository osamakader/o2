#include <stdint.h>

#define NCPU 8
__attribute__((aligned(16), section(".stack"))) char stack0[4096 * NCPU];

void start(void) {

    while (1) {
        asm volatile("wfi");
    }
}