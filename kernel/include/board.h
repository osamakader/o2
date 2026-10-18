#ifndef BOARD_H
#define BOARD_H

#include "types.h"
#include "param.h"

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
};

extern struct board board;

void board_init(uint64 dtb);
void board_report(void);
int board_reserved(uint64 a, uint64 b);

#endif
