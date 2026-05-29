#include "gicv3.h"
#include "console.h"

#define GICD_BASE 0x08000000
#define GICR_OFFSET 0xA0000

#define GICD_CTRL 0x0000

#define GICR_CTLR  (GICR_OFFSET + 0x0000)
#define GICR_WAKER (GICR_OFFSET + 0x0014)

/*
 * Redistributor frame offsets from RD_base
 */
 #define GICR_SGI_OFFSET 0x10000

 /* SGI and PPI Redistributor registers, offsets from RD_base */
#define GICR_IGROUPR0         (GICR_OFFSET + GICR_SGI_OFFSET + 0x0080)
#define GICR_ISENABLER0       (GICR_OFFSET + GICR_SGI_OFFSET + 0x0100)
#define GICR_IPRIORITYR       (GICR_OFFSET + GICR_SGI_OFFSET + 0x0400)

#define GICR_WAKER_ProcessorSleep    (1U << 1)
#define GICR_WAKER_ChildrenAsleep    (1U << 2)
#define GICR_TIMER_IRQ               (1U << 27)

struct gicv3 {
    uint64 base_address;
};

static volatile uint32 *reg(const struct gicv3 *dev, uint32 offset)
{
    const uint64 addr = dev->base_address + offset;

    return (volatile uint32 *)((void *)addr);
}

static void write_reg(const struct gicv3 *dev, uint32 offset, uint32 value)
{
    *reg(dev, offset) = value;
}

static uint32 read_reg(const struct gicv3 *dev, uint32 offset)
{
    return *reg(dev, offset);
}

static inline uint64 read_icc_sre_el1() { 
    uint64 v;
    __asm__ volatile ("mrs %0, " "S3_0_C12_C12_5" : "=r" (v));
    return v;
}

static inline void write_icc_sre_el1(uint64 v) { 
    __asm__ volatile ("msr " "S3_0_C12_C12_5" ", %0" : : "r" (v));
}

static inline uint64 read_icc_pmr_el1() { 
    uint64 v;
    __asm__ volatile ("mrs %0, " "S3_0_C4_C6_0" : "=r" (v));
    return v;
}

static inline void write_icc_pmr_el1(uint64 v) { 
    __asm__ volatile ("msr " "S3_0_C4_C6_0" ", %0" : : "r" (v));
}

static inline uint64 read_icc_igrpen1_el1() { 
    uint64 v;
    __asm__ volatile ("mrs %0, " "S3_0_C12_C12_7" : "=r" (v));
    return v;
}

static inline void write_icc_igrpen1_el1(uint64 v) { 
    __asm__ volatile ("msr " "S3_0_C12_C12_7" ", %0" : : "r" (v));
}

static inline void isb() {
    __asm__ volatile ("isb");
}

void init_gicv3() {
    log_msg("Initializing GICv3\n");

    struct gicv3 gicv3 = {
        .base_address = GICD_BASE,
    };

    // enable the distributor
    write_reg(&gicv3, GICD_CTRL, (1 << 0) | (1 << 1));

    // wake up the redistributor
    uint32 waker = read_reg(&gicv3, GICR_WAKER);
    waker &= ~ (GICR_WAKER_ProcessorSleep);
    write_reg(&gicv3, GICR_WAKER, waker);
    while (read_reg(&gicv3, GICR_WAKER) & GICR_WAKER_ChildrenAsleep);

    // group timer interrupt
    uint32 igroup0 = read_reg(&gicv3, GICR_IGROUPR0);
    igroup0 |= GICR_TIMER_IRQ;
    write_reg(&gicv3, GICR_IGROUPR0, igroup0);

    // enable timer interrupt
    write_reg(&gicv3, GICR_ISENABLER0, GICR_TIMER_IRQ); 

    // enable the processor interface
    write_icc_sre_el1(1);
    isb();
    write_icc_pmr_el1(0xFF);
    isb();
    write_icc_igrpen1_el1(1);
    isb();
}

void enable_irqs() {
    log_msg("enabling interrupts\n");
    __asm__ volatile ("msr daifclr, #0x3");

}
