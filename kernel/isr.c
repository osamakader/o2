#include "console.h"
#include "isr.h"
#include "timer.h"

uint64_t ticks;

void timer_irq()
{
  log_msg("ticks: ");
  ticks++;
  log_uint(ticks);
  log_msg("\n");

  rearm_timer();
}

// interrupts handler
void handle_irq(struct trapframe *tf)
{
  log_msg("handling interrupt\n");

  uint32_t id = 0;
  asm volatile("mrs %0, ICC_IAR1_EL1" : "=r" (id));

  // timer interrupt
  if (id == TIMER_IRQ) 
    timer_irq();
  else
   log_msg("Unknown interrupt\n");

  asm volatile("msr ICC_EOIR1_EL1, %0" : : "r" (id));
}
