#ifndef BOARD_H
#define BOARD_H

#include "types.h"

// The machine, as the device tree describes it (board.c).
struct board {
    uint64 fdt;                 // device tree's physical address, or 0
    char model[64];
    uint64 ram_start, ram_end;  // RAM [start, end), physical
};

extern struct board board;

void board_init(uint64 dtb);
void board_report(void);

#endif
