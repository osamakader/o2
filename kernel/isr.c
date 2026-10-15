#include "console.h"
#include "isr.h"
#include "timer.h"

uint64 ticks;

void timer_irq()
{
  ticks++;
  rearm_timer();
}

// interrupts handler
void handle_irq(struct trapframe *tf)
{
  uint32 id = 0;
  asm volatile("mrs %0, ICC_IAR1_EL1" : "=r" (id));

  // 1020-1023 are special/spurious INTIDs: nothing to handle or EOI.
  if (id >= 1020)
    return;

  // timer interrupt
  if (id == TIMER_IRQ) 
    timer_irq();
  else
   log_msg("Unknown interrupt\n");

  asm volatile("msr ICC_EOIR1_EL1, %0" : : "r" (id));
}
