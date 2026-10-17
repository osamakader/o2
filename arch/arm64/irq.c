// Interrupt dispatch through the GICv3 CPU interface.

#include "types.h"
#include "param.h"
#include "memlayout.h"
#include "console.h"
#include "proc.h"
#include "cpu.h"
#include "timer.h"
#include "intc.h"
#include "trap.h"

#define TIMER_IRQ 27   // EL1 virtual timer PPI

void timer_rearm(void);

// IRQ from EL1 or EL0 (via usertrap_irq()).
void handle_irq(struct trapframe *tf)
{
  uint32 id = 0;
  asm volatile("mrs %0, ICC_IAR1_EL1" : "=r" (id));

  // 1020-1023 are special/spurious INTIDs: nothing to handle or EOI.
  if (id >= 1020)
    return;

  switch (id) {
  case TIMER_IRQ:
    clockintr();
    timer_rearm();
    break;
  default:
    if (!irq_handle(id))
      printf("unexpected interrupt %d on cpu %d\n", id, cpuid());
    break;
  }

  asm volatile("msr ICC_EOIR1_EL1, %0" : : "r" (id));

  // Preempt the running thread on each tick. This runs on the thread's
  // kstack, after EOI so the GIC keeps delivering ticks while it is
  // switched out; eret later restores its IRQ state from SPSR.
  struct proc *p = myproc();
  if (id == TIMER_IRQ && p && p->state == RUNNING)
    yield();
}
