#ifndef TIMER_H
#define TIMER_H

#include "types.h"
#include "console.h"

extern uint64 ticks;

void init_timer();
void disable_timer();
void halt();
void rearm_timer();

#endif