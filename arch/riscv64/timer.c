// Per-hart timer interrupts at HZ, programmed through SBI.

#include "types.h"
#include "memlayout.h"
#include "timer.h"
#include "cpu.h"
#include "sbi.h"
#include "board.h"

// the time CSR's frequency: from the device tree, else qemu's
static uint64 timebase(void)
{
    return board.timebase ? board.timebase : TIMEBASE_FREQ;
}

// Schedule this hart's next tick.
void timer_rearm(void)
{
    sbi_set_timer(csr_read(time) + timebase() / HZ);
}

// Nanoseconds counted by the time CSR since it started (at boot), the
// same on all harts.
uint64 timer_ns(void)
{
    uint64 t = csr_read(time);
    return t / timebase() * 1000000000UL +
           t % timebase() * 1000000000UL / timebase();
}

// Start this hart's tick.
void timerinithart(void)
{
    timer_rearm();
    csr_set(sie, SIE_STIE);
}
