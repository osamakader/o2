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
    uint64 timebase;            // riscv64 timer frequency (Hz), or 0
    int psci_smc;               // arm64: PSCI calls through smc, not hvc

    // devices: physical register addresses and interrupt numbers
    uint64 uart;
    int uart_irq;
    uint64 rtc;                 // 0: none
    uint64 intc, intc_size;     // GICv3 distributor, or the PLIC
    uint64 intc2, intc2_size;   // GICv3 redistributors
    int nvirtio;                // virtio-mmio transports
    struct { uint64 base; int irq; } virtio[32];
};

extern struct board board;

void board_init(uint64 dtb);
void board_report(void);
int board_reserved(uint64 a, uint64 b);
void board_map(pagetable_t pagetable);

#endif
