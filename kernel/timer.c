#include "timer.h"

void init_timer() {
    log_msg("Init timer\n");

    uint64_t freq;
    asm volatile("mrs %0, CNTFRQ_EL0" : "=r" (freq));
    asm volatile("msr CNTV_TVAL_EL0, %0" : : "r" (10000));
    uint64_t ctl = 1;
    asm volatile("msr CNTV_CTL_EL0, %0" : : "r" (ctl));
    asm volatile("isb");
}

void disable_timer() {
    uint64_t ctl = 0;
    asm volatile("msr CNTV_CTL_EL0, %0" : : "r" (ctl));
}

void halt() {
    while (1) {
        asm volatile("wfi");
    }
}
