#ifndef TIMER_H
#define TIMER_H

#include "types.h"

// timer interrupts per second
#define HZ 100

struct spinlock;

// clock.c
extern uint64 ticks;
extern struct spinlock tickslock;
void clockintr(void);
void sleep_ticks(uint64 n);

// wall-clock time
void timeinit(void);
uint64 time_ns(void);    // since the epoch (1970-01-01 UTC)
uint64 time_sec(void);

// arch/<arch>/timer.c: this CPU's periodic timer interrupt at HZ
void timerinithart(void);
uint64 timer_ns(void);    // monotonic, since boot

// arch/<arch>/rtc.c: the real-time clock, ns since the epoch
uint64 rtc_read_ns(void);

#endif
