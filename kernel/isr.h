#ifndef ISR_H
#define ISR_H

#define TIMER_IRQ 27
#include "trap.h"

void timer_irq();
void handle_irq(struct trapframe *tf);

#endif