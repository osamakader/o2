// What the machine looks like, from the device tree when there is one.

#include "types.h"
#include "param.h"
#include "memlayout.h"
#include "defs.h"
#include "console.h"
#include "fdt.h"
#include "board.h"
#include "vm.h"
#include "mmu.h"

struct board board = {
    .ram_start = KERNBASE,
    .ram_end = KERNBASE + DEFAULT_RAM,
    .uart = UART0,
    .uart_irq = UART0_IRQ,
    .rtc = RTC0,
    .intc_kind = INTC_KIND,
    .intc = INTC_BASE,
    .intc_size = INTC_SIZE,
    .intc2 = INTC2_BASE,
    .intc2_size = INTC2_SIZE,
};

// Without a device tree: qemu virt's virtio-mmio slots.
static void default_virtio(void) {
    for (int i = 0; i < VIRTIO_MMIO_COUNT && i < NELEM(board.virtio); i++) {
        board.virtio[i].base = VIRTIO_MMIO + i * VIRTIO_MMIO_STRIDE;
        board.virtio[i].irq = VIRTIO_MMIO_IRQ0 + i;
    }
    board.nvirtio = VIRTIO_MMIO_COUNT < NELEM(board.virtio) ? VIRTIO_MMIO_COUNT : NELEM(board.virtio);
}

static int intc_node = -1;   // the interrupt controller's node

// The interrupt number of node's first interrupt, or -1: for the GIC
// (3 cells) an SPI or PPI number plus 32 or 16, for the PLIC (1 cell)
// the source number.
static int node_irq(int node) {
    int len;
    const char *p = fdt_getprop(node, "interrupts", &len);
    uint32 n = fdt_cells(intc_node, "#interrupt-cells", 1);
    if (p == 0 || len < 4 * n)
        return -1;
    if (n == 3)
        return fdt32(p + 4) + (fdt32(p) == 0 ? 32 : 16);
    return fdt32(p);
}

// The first node compatible with compat: its i-th register range's
// address in *addr (and size in *size, if not 0). Returns the node or
// -1, leaving *addr alone then.
static int find_device(const char *compat, int i, uint64 *addr, uint64 *size) {
    int n = fdt_find_compatible(-1, compat);
    uint64 a, s;
    if (n < 0 || fdt_reg(n, i, &a, &s) < 0)
        return -1;
    *addr = a;
    if (size)
        *size = s;
    return n;
}

static void find_devices(void) {
    static const char *intcs[] = INTC_COMPATS;
    for (int i = 0; i < NELEM(intcs) && intc_node < 0; i++)
        if ((intc_node = find_device(intcs[i], 0, &board.intc, &board.intc_size)) >= 0)
            board.intc_kind = i;
    if (intc_node >= 0 && INTC2_SIZE)
        fdt_reg(intc_node, 1, &board.intc2, &board.intc2_size);

    int n = find_device(UART_COMPAT, 0, &board.uart, 0);
    if (n >= 0 && node_irq(n) >= 0)
        board.uart_irq = node_irq(n);

    board.rtc = 0;
    find_device(RTC_COMPAT, 0, &board.rtc, 0);

    board.nvirtio = 0;
    for (n = fdt_find_compatible(-1, "virtio,mmio"); n >= 0 && board.nvirtio < NELEM(board.virtio);
         n = fdt_find_compatible(n, "virtio,mmio")) {
        uint64 size;
        if (fdt_reg(n, 0, &board.virtio[board.nvirtio].base, &size) == 0 && node_irq(n) >= 0) {
            board.virtio[board.nvirtio].irq = node_irq(n);
            board.nvirtio++;
        }
    }
}

// Map the devices' registers into the kernel page table, at P2V(); pages
// already mapped (two devices in one page) are left as they are.
static void map_range(pagetable_t pagetable, uint64 pa, uint64 size) {
    for (uint64 a = PGROUNDDOWN(pa); a < pa + size; a += PGSIZE) {
        pte_t *pte = walk(pagetable, P2V(a), 0);
        if (pte == 0 || (*pte & PTE_VALID) == 0)
            kvmmap(pagetable, P2V(a), a, PGSIZE, PTE_KERN_DEVICE);
    }
}

void board_map(pagetable_t pagetable) {
    map_range(pagetable, board.uart, PGSIZE);
    if (board.rtc)
        map_range(pagetable, board.rtc, PGSIZE);
    map_range(pagetable, board.intc, board.intc_size);
    if (board.intc2_size)
        map_range(pagetable, board.intc2, board.intc2_size);
    for (int i = 0; i < board.nvirtio; i++)
        map_range(pagetable, board.virtio[i].base, PGSIZE);
}

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
    if (fdt_init(dtb) < 0 && fdt_init(KERNBASE) < 0) {
        default_virtio();
        return;
    }
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
        if (board.ncpu < NCPU) {
            board.cpu[board.ncpu] = id;
            const char *method = fdt_getprop(n, "enable-method", 0);
            int len;
            const char *rel = fdt_getprop(n, "cpu-release-addr", &len);
            if (method && strncmp(method, "spin-table", 11) == 0 && rel && len == 8)
                board.cpu_release[board.ncpu] = (uint64)fdt32(rel) << 32 | fdt32(rel + 4);
        }
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

    find_devices();

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
    printf("board: uart %p irq %d, rtc %p, intc %p", (void *)board.uart, board.uart_irq,
           (void *)board.rtc, (void *)board.intc);
    if (board.intc2_size)
        printf(" + %p", (void *)board.intc2);
    printf(", %d virtio-mmio", board.nvirtio);
    if (board.nvirtio)
        printf(" from %p irq %d", (void *)board.virtio[0].base, board.virtio[0].irq);
    printf("\n");
}
