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

// arch/<arch>/timer.c: this CPU's periodic timer interrupt at HZ
void timerinithart(void);

#endif
