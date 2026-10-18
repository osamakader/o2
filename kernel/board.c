// What the machine looks like, from the device tree when there is one.

#include "types.h"
#include "param.h"
#include "memlayout.h"
#include "defs.h"
#include "console.h"
#include "fdt.h"
#include "board.h"

struct board board;

// Read the device tree at physical address dtb (0: look at the start of
// RAM, where qemu puts it for arm64 ELF kernels). Called first thing at
// boot, before the console works: board_report() prints the results.
void board_init(uint64 dtb) {
    if (fdt_init(dtb) < 0 && fdt_init(KERNBASE) < 0)
        return;
    board.fdt = dtb ? dtb : KERNBASE;

    int root = fdt_next_node(-1, 0), len;
    const char *model = fdt_getprop(root, "model", &len);
    if (model)
        safestrcpy(board.model, model, sizeof(board.model));

    // RAM: the first memory node's first range
    for (int n = fdt_next_node(-1, 0); n >= 0; n = fdt_next_node(n, 0)) {
        const char *type = fdt_getprop(n, "device_type", 0);
        uint64 addr, size;
        if (type && strncmp(type, "memory", 7) == 0 && fdt_reg(n, 0, &addr, &size) == 0) {
            board.ram_start = addr;
            board.ram_end = addr + size;
            break;
        }
    }
}

void board_report(void) {
    if (board.fdt == 0) {
        printf("board: no device tree, using the built-in qemu virt layout\n");
        return;
    }
    printf("board: %s, device tree at %p (%lu bytes)\n", board.model[0] ? board.model : "?",
           (void *)board.fdt, fdt_size());
    printf("board: RAM %p-%p (%lu MB)\n", (void *)board.ram_start, (void *)board.ram_end,
           (board.ram_end - board.ram_start) >> 20);
}
