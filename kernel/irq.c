// Device interrupt handlers, by interrupt-controller IRQ number. The
// arch dispatch code (arch/<arch>/irq.c, trap.c) acknowledges the
// interrupt and calls irq_handle().

#include "types.h"
#include "console.h"
#include "intc.h"

static void (*handlers[NIRQ])(void);

// Call fn for interrupts from irq, delivered to cpu. Registered once
// at boot, before interrupts are taken.
void irq_register(uint32 irq, void (*fn)(void), int cpu)
{
    if (irq >= NIRQ)
        panic("irq_register: bad irq %d", irq);
    handlers[irq] = fn;
    intc_enable(irq, cpu);
}

// Run the handler for irq. Returns 0 if there is none.
int irq_handle(uint32 irq)
{
    if (irq >= NIRQ || handlers[irq] == 0)
        return 0;
    handlers[irq]();
    return 1;
}
