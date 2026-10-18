// PL031 real-time clock. Only read, at boot (timeinit()): RTCDR counts
// seconds since the epoch.

#include "types.h"
#include "memlayout.h"
#include "timer.h"
#include "board.h"

#define RTCDR 0x00

uint64 rtc_read_ns(void) {
    if (board.rtc == 0)
        return 0;
    uint32 sec = *(volatile uint32 *)(P2V(board.rtc) + RTCDR);
    return (uint64)sec * 1000000000UL;
}
