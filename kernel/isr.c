#include "console.h"
#include "isr.h"
#include "timer.h"
#include "proc.h"
#include "cpu.h"
#include "spinlock.h"

// Timer interrupts seen by CPU 0. Every CPU has its own timer, so
// counting on all of them would advance ticks ncpu times per tick.
// Updated under tickslock; sleepers wait on &ticks.
struct spinlock tickslock = { .name = "time" };
uint64 ticks;

void timer_irq()
{
  if (cpuid() == 0) {
    acquire(&tickslock);
    ticks++;
    wakeup(&ticks);
    release(&tickslock);
  }
  rearm_timer();
}

// Block the calling thread for at least n ticks.
void sleep_ticks(uint64 n)
{
  acquire(&tickslock);
  uint64 ticks0 = ticks;
  while (ticks - ticks0 < n)
    sleep(&ticks, &tickslock);
  release(&tickslock);
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

  // Preempt the running thread on each tick. This runs on the thread's
  // kstack, after EOI so the GIC keeps delivering ticks while it is
  // switched out; eret later restores its IRQ state from SPSR.
  struct proc *p = myproc();
  if (id == TIMER_IRQ && p && p->state == RUNNING)
    yield();
}
