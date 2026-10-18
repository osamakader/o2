// GICv2 interrupt controller (the Raspberry Pi's GIC-400): a
// distributor, and a memory-mapped CPU interface for each CPU at the
// same address. The firmware leaves interrupts in the non-secure group,
// which is what o2 sees from non-secure EL1.

#include "types.h"
#include "intc.h"
#include "console.h"
#include "memlayout.h"
#include "cpu.h"
#include "board.h"
#include "gic.h"

#define GICD_BASE P2V(board.intc)
#define GICC_BASE P2V(board.intc2)

// distributor registers
#define GICD_CTLR       0x000
#define GICD_ISENABLER  0x100   // 1 bit per interrupt
#define GICD_IPRIORITYR 0x400   // 1 byte per interrupt
#define GICD_ITARGETSR  0x800   // 1 byte per interrupt: a CPU mask
#define GICD_ICFGR      0xc00   // 2 bits per interrupt

// CPU interface registers
#define GICC_CTLR 0x000
#define GICC_PMR  0x004
#define GICC_IAR  0x00c
#define GICC_EOIR 0x010

#define GIC_PRIO_DEFAULT 0xa0
#define TIMER_PPI        27     // EL1 virtual timer

static volatile uint32 *reg(uint64 base, uint32 offset)
{
    return (volatile uint32 *)(base + offset);
}

void gicv2_init(void)
{
    log_msg("Initializing GICv2\n");
    *reg(GICD_BASE, GICD_CTLR) = 1;   // forward interrupts to the CPUs
}

// Per-CPU: the timer PPI (banked per CPU), and the CPU interface.
void gicv2_inithart(void)
{
    *(volatile uint8 *)(GICD_BASE + GICD_IPRIORITYR + TIMER_PPI) = GIC_PRIO_DEFAULT;
    *reg(GICD_BASE, GICD_ISENABLER) = 1U << TIMER_PPI;
    *reg(GICC_BASE, GICC_PMR) = 0xff;  // let all priorities through
    *reg(GICC_BASE, GICC_CTLR) = 1;
}

// Configure SPI irq as level-triggered, send it to CPU cpu, enable it.
void gicv2_enable(uint32 irq, int cpu)
{
    if (irq < 32 || irq >= 1020)
        panic("gicv2_enable: bad SPI %d", irq);
    *(volatile uint8 *)(GICD_BASE + GICD_IPRIORITYR + irq) = GIC_PRIO_DEFAULT;
    *(volatile uint8 *)(GICD_BASE + GICD_ITARGETSR + irq) = 1 << cpu;
    uint32 cfg = 4 * (irq / 16), edge = 1U << (2 * (irq % 16) + 1);
    *reg(GICD_BASE, GICD_ICFGR + cfg) &= ~edge;
    *reg(GICD_BASE, GICD_ISENABLER + 4 * (irq / 32)) = 1U << (irq % 32);
}

uint32 gicv2_ack(void)
{
    return *reg(GICC_BASE, GICC_IAR) & 0x3ff;
}

void gicv2_eoi(uint32 id)
{
    *reg(GICC_BASE, GICC_EOIR) = id;
}
