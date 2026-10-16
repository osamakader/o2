#ifndef TIMER_H
#define TIMER_H

#include "types.h"
#include "console.h"

// timer interrupts per second
#define HZ 100

struct spinlock;

extern uint64 ticks;
extern struct spinlock tickslock;

void sleep_ticks(uint64 n);

void init_timer();
void disable_timer();
void halt();
void rearm_timer();

#endif