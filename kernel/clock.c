// The system clock: ticks counted from the timer interrupt.

#include "types.h"
#include "param.h"
#include "spinlock.h"
#include "proc.h"
#include "timer.h"
#include "cpu.h"

// Timer interrupts seen by CPU 0. Every CPU has its own timer, so
// counting on all of them would advance ticks ncpu times per tick.
// Updated under tickslock; sleepers wait on &ticks.
struct spinlock tickslock = { .name = "time" };
uint64 ticks;

// Called by the arch timer interrupt handler on every CPU.
void clockintr(void)
{
    if (cpuid() == 0) {
        acquire(&tickslock);
        ticks++;
        wakeup(&ticks);
        release(&tickslock);
    }
}

// Block the calling thread for at least n ticks.
void sleep_ticks(uint64 n)
{
    acquire(&tickslock);
    uint64 ticks0 = ticks;
    while (ticks - ticks0 < n)
        sleep(&ticks, &tickslock);
    release(&tickslock);
}
