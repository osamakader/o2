#include "gicv3.h"
#include "console.h"
#include "memlayout.h"
#include "cpu.h"

#define GICD_BASE P2V(GICD)
#define GICR_BASE P2V(GICR)

// Distributor registers
#define GICD_CTLR 0x0000

// Redistributor RD_base frame registers
#define GICR_TYPER 0x0008      // 64-bit
#define GICR_WAKER 0x0014

#define GICR_TYPER_LAST      (1UL << 4)
#define GICR_TYPER_AFF0(t)   (((t) >> 32) & 0xff)

#define GICR_WAKER_ProcessorSleep    (1U << 1)
#define GICR_WAKER_ChildrenAsleep    (1U << 2)

// Each redistributor is an RD_base frame followed by an SGI_base frame
// holding the SGI/PPI registers.
#define GICR_FRAME_SIZE  0x20000
#define GICR_SGI_OFFSET  0x10000
#define GICR_IGROUPR0    (GICR_SGI_OFFSET + 0x0080)
#define GICR_ISENABLER0  (GICR_SGI_OFFSET + 0x0100)

#define GICR_TIMER_IRQ   (1U << 27)

static volatile uint32 *reg(uint64 base, uint32 offset)
{
    return (volatile uint32 *)(base + offset);
}

static void write_reg(uint64 base, uint32 offset, uint32 value)
{
    *reg(base, offset) = value;
}

static uint32 read_reg(uint64 base, uint32 offset)
{
    return *reg(base, offset);
}

static inline void write_icc_sre_el1(uint64 v) {
    __asm__ volatile ("msr " "S3_0_C12_C12_5" ", %0" : : "r" (v));
}

static inline void write_icc_pmr_el1(uint64 v) {
    __asm__ volatile ("msr " "S3_0_C4_C6_0" ", %0" : : "r" (v));
}

static inline void write_icc_igrpen1_el1(uint64 v) {
    __asm__ volatile ("msr " "S3_0_C12_C12_7" ", %0" : : "r" (v));
}

static inline void isb() {
    __asm__ volatile ("isb");
}

// Find this CPU's redistributor by matching GICR_TYPER's affinity
// against MPIDR Aff0.
static uint64 gicr_base(void)
{
    int id = cpuid();

    for (uint64 base = GICR_BASE; base < GICR_BASE + GICR_SIZE; base += GICR_FRAME_SIZE) {
        uint64 typer = *(volatile uint64 *)(base + GICR_TYPER);
        if (GICR_TYPER_AFF0(typer) == id)
            return base;
        if (typer & GICR_TYPER_LAST)
            break;
    }
    panic("gicr_base: no redistributor");
}

// Global GIC setup, once, on the boot CPU.
void gic_init() {
    log_msg("Initializing GICv3\n");

    // enable the distributor (group 0 and non-secure group 1)
    write_reg(GICD_BASE, GICD_CTLR, (1 << 0) | (1 << 1));
}

// Per-CPU setup: this CPU's redistributor and CPU interface.
void gic_inithart() {
    uint64 rd = gicr_base();

    // wake up the redistributor
    uint32 waker = read_reg(rd, GICR_WAKER);
    waker &= ~(GICR_WAKER_ProcessorSleep);
    write_reg(rd, GICR_WAKER, waker);
    while (read_reg(rd, GICR_WAKER) & GICR_WAKER_ChildrenAsleep)
        ;

    // timer interrupt: group 1, enabled
    uint32 igroup0 = read_reg(rd, GICR_IGROUPR0);
    igroup0 |= GICR_TIMER_IRQ;
    write_reg(rd, GICR_IGROUPR0, igroup0);
    write_reg(rd, GICR_ISENABLER0, GICR_TIMER_IRQ);

    // enable the CPU interface: system registers, all priorities,
    // group 1 interrupts
    write_icc_sre_el1(1);
    isb();
    write_icc_pmr_el1(0xFF);
    isb();
    write_icc_igrpen1_el1(1);
    isb();
}
