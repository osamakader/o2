#ifndef BOARD_H
#define BOARD_H

#include "types.h"
#include "param.h"
#include "vm.h"

// The machine, as the device tree describes it (board.c).
struct board {
    uint64 fdt;                 // device tree's physical address, or 0
    char model[64];
    uint64 ram_start, ram_end;  // RAM [start, end), physical
    int nreserved;              // memory not to use: [start, end)
    struct { uint64 start, end; } reserved[8];
    int ncpu;                   // CPUs: hart ids (riscv64) or MPIDRs
    uint64 cpu[NCPU];
    uint64 cpu_release[NCPU];   // arm64 spin table: where to write the
                                // entry point to start it; 0: PSCI
    uint64 timebase;            // riscv64 timer frequency (Hz), or 0
    int psci_smc;               // arm64: PSCI calls through smc, not hvc

    // devices: physical register addresses and interrupt numbers
    uint64 uart;
    int uart_irq;
    uint64 rtc;                 // 0: none
    int intc_kind;              // which one (platform.h's INTC_COMPATS)
    uint64 intc, intc_size;     // GIC distributor, or the PLIC
    uint64 intc2, intc2_size;   // GICv3 redistributors, GICv2 CPU interface
    uint64 sdhci[2];            // SD host controllers that may have the
                                // card, in order; 0: none
    int nvirtio;                // virtio-mmio transports
    struct { uint64 base; int irq; } virtio[32];
};

extern struct board board;

void board_init(uint64 dtb);
void board_report(void);
int board_reserved(uint64 a, uint64 b);
void board_map(pagetable_t pagetable);

#endif
