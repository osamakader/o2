// The RISC-V Platform Level Interrupt Controller (PLIC). Each hart has
// an S-mode context (2 * hart + 1 on qemu virt). After xv6.

#include "types.h"
#include "param.h"
#include "memlayout.h"
#include "intc.h"
#include "cpu.h"
#include "board.h"

#define PLIC_BASE P2V(board.intc)
#define PLIC_PRIORITY(irq)   (PLIC_BASE + (irq) * 4)
#define PLIC_SENABLE(hart)   (PLIC_BASE + 0x2080 + (hart) * 0x100)
#define PLIC_SPRIORITY(hart) (PLIC_BASE + 0x201000 + (hart) * 0x2000)
#define PLIC_SCLAIM(hart)    (PLIC_BASE + 0x201004 + (hart) * 0x2000)

#define Reg(a) (*(volatile uint32 *)(a))

void intc_init(void)
{
}

// Accept interrupts of any priority on this hart.
void intc_inithart(void)
{
    Reg(PLIC_SPRIORITY(cpuid())) = 0;
    csr_set(sie, SIE_SEIE);
}

// Give source irq a non-zero priority and deliver it to hart cpu.
void intc_enable(uint32 irq, int cpu)
{
    Reg(PLIC_PRIORITY(irq)) = 1;
    Reg(PLIC_SENABLE(cpu) + 4 * (irq / 32)) |= 1U << (irq % 32);
}

// ask the PLIC what interrupt we should serve.
int plic_claim(void)
{
    return Reg(PLIC_SCLAIM(cpuid()));
}

// tell the PLIC we've served this IRQ.
void plic_complete(int irq)
{
    Reg(PLIC_SCLAIM(cpuid())) = irq;
}
