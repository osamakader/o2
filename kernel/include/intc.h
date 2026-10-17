#ifndef INTC_H
#define INTC_H

#include "types.h"

// The interrupt controller, implemented per arch (GICv3 on arm64,
// PLIC on riscv64).

void intc_init(void);                 // global setup, once, boot CPU
void intc_inithart(void);             // per-CPU setup
void intc_enable(uint32 irq, int cpu); // route a device interrupt to cpu

// kernel/irq.c: device interrupt handlers
#define NIRQ 1024
void irq_register(uint32 irq, void (*fn)(void), int cpu);
int irq_handle(uint32 irq);

#endif
