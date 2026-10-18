// What the machine looks like, from the device tree when there is one.

#include "types.h"
#include "param.h"
#include "memlayout.h"
#include "defs.h"
#include "console.h"
#include "fdt.h"
#include "board.h"

struct board board = {
    .ram_start = KERNBASE,
    .ram_end = KERNBASE + DEFAULT_RAM,
};

static void reserve(uint64 addr, uint64 size) {
    if (board.nreserved < NELEM(board.reserved) && size > 0) {
        board.reserved[board.nreserved].start = addr;
        board.reserved[board.nreserved].end = addr + size;
        board.nreserved++;
    }
}

// Is any of physical [a, b) reserved?
int board_reserved(uint64 a, uint64 b) {
    for (int i = 0; i < board.nreserved; i++)
        if (board.reserved[i].start < b && a < board.reserved[i].end)
            return 1;
    return 0;
}

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

    // RAM: the memory node's range starting at KERNBASE, where the
    // kernel is linked to run
    for (int n = fdt_next_node(-1, 0); n >= 0; n = fdt_next_node(n, 0)) {
        const char *type = fdt_getprop(n, "device_type", 0);
        uint64 addr, size;
        if (type && strncmp(type, "memory", 7) == 0 && fdt_reg(n, 0, &addr, &size) == 0 &&
            addr == KERNBASE) {
            board.ram_end = addr + (size < MAX_RAM ? size : MAX_RAM);
            break;
        }
    }

    // the CPUs: /cpus/cpu@...
    for (int n = fdt_next_node(-1, 0); n >= 0; n = fdt_next_node(n, 0)) {
        const char *type = fdt_getprop(n, "device_type", 0);
        const char *status = fdt_getprop(n, "status", 0);
        uint64 id, size;
        if (type == 0 || strncmp(type, "cpu", 4) != 0 || fdt_reg(n, 0, &id, &size) < 0)
            continue;
        if (status && strncmp(status, "okay", 5) != 0 && strncmp(status, "ok", 3) != 0)
            continue;
        if (board.ncpu < NCPU)
            board.cpu[board.ncpu] = id;
        board.ncpu++;
    }
    int cpus = fdt_next_node(-1, 0);
    while (cpus >= 0 && strncmp(fdt_name(cpus), "cpus", 5) != 0)
        cpus = fdt_next_node(cpus, 0);
    board.timebase = fdt_cells(cpus, "timebase-frequency", 0);

    const char *method;
    for (int n = fdt_next_node(-1, 0); n >= 0; n = fdt_next_node(n, 0))
        if (strncmp(fdt_name(n), "psci", 4) == 0 && (method = fdt_getprop(n, "method", 0)))
            board.psci_smc = strncmp(method, "smc", 4) == 0;

    // memory the firmware keeps for itself
    uint64 addr, size;
    for (int i = 0; fdt_memreserve(i, &addr, &size) == 0; i++)
        reserve(addr, size);
    int rm = -1;
    for (int n = fdt_next_node(-1, 0); n >= 0; n = fdt_next_node(n, 0)) {
        if (strncmp(fdt_name(n), "reserved-memory", 16) == 0)
            rm = n;
        else if (rm >= 0 && fdt_parent(n) == rm)
            for (int i = 0; fdt_reg(n, i, &addr, &size) == 0; i++)
                reserve(addr, size);
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
    for (int i = 0; i < board.nreserved; i++)
        printf("board: reserved %p-%p\n", (void *)board.reserved[i].start,
               (void *)board.reserved[i].end);
    printf("board: %d cpus", board.ncpu);
    if (board.ncpu > NCPU)
        printf(", using %d (NCPU)", NCPU);
    if (board.timebase)
        printf(", timebase %lu Hz", board.timebase);
    printf("\n");
}
