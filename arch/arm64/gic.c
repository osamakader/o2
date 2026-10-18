// The interrupt controller: GICv3 or GICv2, whichever the device tree
// describes (board.intc_kind: 0 a GICv3, else a GICv2).

#include "types.h"
#include "intc.h"
#include "board.h"
#include "gic.h"

void intc_init(void)
{
    if (board.intc_kind == 0)
        gicv3_init();
    else
        gicv2_init();
}

void intc_inithart(void)
{
    if (board.intc_kind == 0)
        gicv3_inithart();
    else
        gicv2_inithart();
}

void intc_enable(uint32 irq, int cpu)
{
    if (board.intc_kind == 0)
        gicv3_enable(irq, cpu);
    else
        gicv2_enable(irq, cpu);
}

uint32 gic_ack(void)
{
    return board.intc_kind == 0 ? gicv3_ack() : gicv2_ack();
}

void gic_eoi(uint32 id)
{
    if (board.intc_kind == 0)
        gicv3_eoi(id);
    else
        gicv2_eoi(id);
}
