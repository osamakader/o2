// Goldfish real-time clock. Only read, at boot (timeinit()): it counts
// nanoseconds since the epoch, and reading TIME_LOW latches TIME_HIGH.

#include "types.h"
#include "memlayout.h"
#include "timer.h"

#define TIME_LOW  0x00
#define TIME_HIGH 0x04

uint64 rtc_read_ns(void)
{
    uint64 lo = *(volatile uint32 *)(P2V(RTC0) + TIME_LOW);
    uint64 hi = *(volatile uint32 *)(P2V(RTC0) + TIME_HIGH);
    return hi << 32 | lo;
}
