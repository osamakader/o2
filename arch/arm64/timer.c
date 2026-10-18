// arm64 generic timer: the EL1 virtual timer, one per CPU, as PPI 27.

#include "types.h"
#include "timer.h"

static void timer_set(void) {
    uint64 freq;
    asm volatile("mrs %0, CNTFRQ_EL0" : "=r" (freq));
    asm volatile("msr CNTV_TVAL_EL0, %0" : : "r" (freq / HZ));
    uint64 ctl = 1;
    asm volatile("msr CNTV_CTL_EL0, %0" : : "r" (ctl));
    asm volatile("isb");
}

// Start this CPU's tick.
void timerinithart(void) {
    timer_set();
}

// Nanoseconds counted by the system counter since it started (at
// boot), the same on all CPUs.
uint64 timer_ns(void) {
    uint64 freq, cnt;
    asm volatile("mrs %0, CNTFRQ_EL0" : "=r" (freq));
    asm volatile("isb; mrs %0, CNTVCT_EL0" : "=r" (cnt));
    return cnt / freq * 1000000000UL + cnt % freq * 1000000000UL / freq;
}

// Timer interrupt: schedule the next tick.
void timer_rearm(void) {
    timer_set();
}
