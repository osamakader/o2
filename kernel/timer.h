#ifndef TIMER_H
#define TIMER_H

#include <stdint.h>
#include "console.h"

extern uint64_t ticks;

void init_timer();
void disable_timer();
void halt();
void rearm_timer();

#endif