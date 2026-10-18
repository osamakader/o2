#ifndef GIC_H
#define GIC_H

#include "types.h"

// The two GICs o2 drives: GICv3 (qemu virt) and GICv2 (the GIC-400 of
// the Raspberry Pi 4 and 5). gic.c picks one (board.intc_kind) behind
// intc.h's functions.

void gicv3_init(void);
void gicv3_inithart(void);
void gicv3_enable(uint32 irq, int cpu);
uint32 gicv3_ack(void);
void gicv3_eoi(uint32 id);

void gicv2_init(void);
void gicv2_inithart(void);
void gicv2_enable(uint32 irq, int cpu);
uint32 gicv2_ack(void);
void gicv2_eoi(uint32 id);

// for irq.c: acknowledge the pending interrupt (1020-1023: none), and
// say it has been handled
uint32 gic_ack(void);
void gic_eoi(uint32 id);

#endif
