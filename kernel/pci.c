// PCI: finding the devices on the PCI(e) bus and giving their memory
// BARs addresses in the host's memory window (no firmware did it).
// Config space is the host's: ECAM (qemu virt: "pci-host-ecam-generic")
// or the Raspberry Pi 4's bridge (pcie_brcm.c). Buses behind bridges
// are numbered and scanned depth first, each bridge's window covering
// what lies behind it.

#include "types.h"
#include "param.h"
#include "memlayout.h"
#include "defs.h"
#include "console.h"
#include "fdt.h"
#include "vm.h"
#include "mmu.h"
#include "pci.h"

// physical addresses P2V() reaches: the top 256GB of virtual space
#define LINEAR_MAP_SIZE (-(uint64)KVA_OFFSET)

struct pci_host pcihost;
struct pci_dev pcidevs[NPCIDEV];
int npcidev;
static int lastbus;

uint32 pci_read(struct pci_dev *d, int off) {
    return pcihost.read(d->bus, d->dev, d->fn, off);
}

void pci_write(struct pci_dev *d, int off, uint32 v) {
    pcihost.write(d->bus, d->dev, d->fn, off, v);
}

// ECAM: each function's 4KB of config space, at bus << 20 | dev << 15 | fn << 12
static uint64 ecam;

static volatile uint32 *ecam_reg(int bus, int dev, int fn, int off) {
    return (volatile uint32 *)P2V(ecam + ((uint64)bus << 20 | dev << 15 | fn << 12 | off));
}

static uint32 ecam_read(int bus, int dev, int fn, int off) {
    return *ecam_reg(bus, dev, fn, off);
}

static void ecam_write(int bus, int dev, int fn, int off, uint32 v) {
    *ecam_reg(bus, dev, fn, off) = v;
}

// The host's 32-bit memory window from its node's "ranges": PCI
// address, CPU address and size. Returns 0, or -1 if there is none.
int pci_window(int node) {
    int len;
    const char *r = fdt_getprop(node, "ranges", &len);
    int parent = fdt_parent(node);
    uint32 pac = fdt_cells(parent, "#address-cells", 2);
    uint32 entry = 4 * (3 + pac + 2);
    for (int off = 0; r && off + entry <= len; off += entry) {
        uint32 space = (fdt32(r + off) >> 24) & 3;   // 2: 32-bit memory
        if (space != 2)
            continue;
        pcihost.mem_pci = (uint64)fdt32(r + off + 4) << 32 | fdt32(r + off + 8);
        uint64 cpu = 0;
        for (int i = 0; i < pac; i++)
            cpu = cpu << 32 | fdt32(r + off + 12 + 4 * i);
        pcihost.mem_cpu = fdt_translate(parent, cpu);
        pcihost.mem_size = (uint64)fdt32(r + off + 12 + 4 * pac) << 32 |
                           fdt32(r + off + 16 + 4 * pac);
        pcihost.next = pcihost.mem_pci;
        return 0;
    }
    return -1;
}

// Room for size bytes (a power of two) in the window, aligned to it:
// its PCI address, or 0.
static uint64 window_alloc(uint64 size) {
    uint64 a = (pcihost.next + size - 1) & ~(size - 1);
    if (a + size > pcihost.mem_pci + pcihost.mem_size)
        return 0;
    pcihost.next = a + size;
    return a;
}

// Size the device's memory BARs and put them in the window.
static void assign_bars(struct pci_dev *d, int nbar) {
    for (int i = 0; i < nbar; i++) {
        int off = PCI_BAR0 + 4 * i;
        uint32 old = pci_read(d, off);
        if (old & 1)
            continue;   // I/O space: not supported
        int is64 = ((old >> 1) & 3) == 2;
        pci_write(d, off, 0xffffffff);
        uint64 mask = pci_read(d, off) & ~0xfUL;
        if (is64) {
            pci_write(d, off + 4, 0xffffffff);
            mask |= (uint64)pci_read(d, off + 4) << 32;
        } else {
            mask |= 0xffffffff00000000UL;
        }
        uint64 size = ~mask + 1;
        uint64 a = mask ? window_alloc(size < 4096 ? 4096 : size) : 0;
        pci_write(d, off, a);
        if (is64)
            pci_write(d, off + 4, a >> 32);
        if (a) {
            d->bar[i] = a - pcihost.mem_pci + pcihost.mem_cpu;
            d->barsize[i] = size;
            kvmmap(kernel_pagetable, P2V(d->bar[i]), d->bar[i], size, PTE_KERN_DEVICE);
        }
        if (is64)
            i++;
    }
}

static void scan_bus(int bus);

static void found(int bus, int dev, int fn) {
    if (npcidev == NPCIDEV)
        return;
    struct pci_dev *d = &pcidevs[npcidev++];
    memset(d, 0, sizeof(*d));
    d->bus = bus;
    d->dev = dev;
    d->fn = fn;
    uint32 id = pci_read(d, PCI_ID);
    d->vendor = id & 0xffff;
    d->device = id >> 16;
    d->class = pci_read(d, PCI_CLASS) >> 8;
    int type = (pci_read(d, PCI_HEADER) >> 16) & 0x7f;

    if (type == 1) {
        // a bridge: number the bus behind it, scan that, and open a
        // window for what is there (1MB granules)
        assign_bars(d, 2);
        int sec = ++lastbus;
        pci_write(d, PCI_BUSES, (pci_read(d, PCI_BUSES) & 0xff000000) | 0xff << 16 | sec << 8 | bus);
        pcihost.next = (pcihost.next + 0xfffff) & ~0xfffffUL;
        uint64 start = pcihost.next;
        scan_bus(sec);
        pcihost.next = (pcihost.next + 0xfffff) & ~0xfffffUL;
        pci_write(d, PCI_BUSES, (pci_read(d, PCI_BUSES) & 0xff000000) | lastbus << 16 | sec << 8 | bus);
        if (pcihost.next > start)
            pci_write(d, PCI_MEMWIN, ((pcihost.next - 1) & 0xfff00000) | (start >> 16 & 0xfff0));
        else
            pci_write(d, PCI_MEMWIN, 0x0000fff0);   // limit below base: closed
        pci_write(d, PCI_COMMAND, pci_read(d, PCI_COMMAND) | PCI_CMD_MEM | PCI_CMD_MASTER);
    } else {
        assign_bars(d, 6);
    }
    printf("pci: %d:%d.%d %x:%x class %x%s\n", bus, dev, fn, d->vendor, d->device, d->class,
           type == 1 ? " (bridge)" : "");
}

static void scan_bus(int bus) {
    for (int dev = 0; dev < 32; dev++) {
        if ((pcihost.read(bus, dev, 0, PCI_ID) & 0xffff) == 0xffff)
            continue;
        int fns = (pcihost.read(bus, dev, 0, PCI_HEADER) >> 16) & 0x80 ? 8 : 1;
        for (int fn = 0; fn < fns; fn++)
            if ((pcihost.read(bus, dev, fn, PCI_ID) & 0xffff) != 0xffff)
                found(bus, dev, fn);
        if (pcihost.onlydev0)
            break;
    }
}

// Find the host and the devices.
void pci_init(void) {
    if (!fdt_present())
        return;
    int n = fdt_find_compatible(-1, "pci-host-ecam-generic");
    uint64 size;
    if (n >= 0 && fdt_okay(n) && fdt_reg(n, 0, &ecam, &size) == 0 && pci_window(n) == 0) {
        if (ecam >= LINEAR_MAP_SIZE) {
            // qemu's arm64 virt puts it high unless highmem-ecam=off
            printf("pci: ECAM at %p is out of the kernel's reach\n", (void *)ecam);
            return;
        }
        // the first 4 buses' config space
        kvmmap(kernel_pagetable, P2V(ecam), ecam, size < 0x400000 ? size : 0x400000, PTE_KERN_DEVICE);
        pcihost.read = ecam_read;
        pcihost.write = ecam_write;
    } else {
        return;
    }
    scan_bus(0);
}

// The first device of the class (base class, subclass, interface), or 0.
struct pci_dev *pci_find(uint32 class) {
    for (int i = 0; i < npcidev; i++)
        if (pcidevs[i].class == class)
            return &pcidevs[i];
    return 0;
}

// Let d answer at its BARs and do DMA.
void pci_enable(struct pci_dev *d) {
    pci_write(d, PCI_COMMAND, pci_read(d, PCI_COMMAND) | PCI_CMD_MEM | PCI_CMD_MASTER);
}
