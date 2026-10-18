// Per-hart timer interrupts at HZ, programmed through SBI.

#include "types.h"
#include "memlayout.h"
#include "timer.h"
#include "cpu.h"
#include "sbi.h"

// Schedule this hart's next tick.
void timer_rearm(void)
{
    sbi_set_timer(csr_read(time) + TIMEBASE_FREQ / HZ);
}

// Nanoseconds counted by the time CSR since it started (at boot), the
// same on all harts.
uint64 timer_ns(void)
{
    uint64 t = csr_read(time);
    return t / TIMEBASE_FREQ * 1000000000UL +
           t % TIMEBASE_FREQ * 1000000000UL / TIMEBASE_FREQ;
}

// Start this hart's tick.
void timerinithart(void)
{
    timer_rearm();
    csr_set(sie, SIE_STIE);
}
