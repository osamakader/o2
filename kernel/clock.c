// The system clock: ticks counted from the timer interrupt, and
// wall-clock time: the real-time clock read at boot plus the time since.

#include "types.h"
#include "param.h"
#include "spinlock.h"
#include "proc.h"
#include "timer.h"
#include "cpu.h"
#include "time.h"
#include "console.h"
#include "defs.h"
#include "socket.h"
#include "net.h"

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
        sock_tick();
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

// Wall-clock time when timer_ns() was 0.
static uint64 boot_ns;

void timeinit(void)
{
    boot_ns = rtc_read_ns() - timer_ns();
    printf("time: %lu s since the epoch\n", time_sec());
}

uint64 time_ns(void)
{
    return boot_ns + timer_ns();
}

uint64 time_sec(void)
{
    return time_ns() / 1000000000UL;
}

// clock_gettime(clock, ts)
uint64 sys_clock_gettime(void)
{
    int clock;
    uint64 addr, ns;
    struct timespec ts;

    argint(0, &clock);
    argaddr(1, &addr);
    if (clock == CLOCK_REALTIME)
        ns = time_ns();
    else if (clock == CLOCK_MONOTONIC)
        ns = timer_ns();
    else
        return -1;
    ts.tv_sec = ns / 1000000000UL;
    ts.tv_nsec = ns % 1000000000UL;
    return copyout(myproc()->pagetable, addr, (char *)&ts, sizeof(ts));
}

// clock_settime(CLOCK_REALTIME, ts): set the wall-clock time (boards
// without an RTC start at the epoch). It doesn't go to the RTC.
uint64 sys_clock_settime(void)
{
    int clock;
    uint64 addr;
    struct timespec ts;

    argint(0, &clock);
    argaddr(1, &addr);
    if (clock != CLOCK_REALTIME || copyin(myproc()->pagetable, (char *)&ts, addr, sizeof(ts)) < 0 ||
        ts.tv_nsec >= 1000000000UL)
        return -1;
    boot_ns = ts.tv_sec * 1000000000UL + ts.tv_nsec - timer_ns();
    return 0;
}
