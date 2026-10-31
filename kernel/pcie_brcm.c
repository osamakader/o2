// The Raspberry Pi 4's PCIe host bridge (BCM2711, "brcm,bcm2711-pcie"),
// whose one lane leads to the VL805 USB controller. Set up as Linux's
// pcie-brcmstb.c does: out of reset, an inbound window through which
// the device reaches RAM, an outbound one (CPU 0x6_0000_0000) through
// which the CPU reaches the device, the link. Config space: the bridge's
// own registers for bus 0, an index/data pair for the bus behind it.
// Then the firmware loads the VL805's firmware (it has no EEPROM).

#include "types.h"
#include "param.h"
#include "memlayout.h"
#include "defs.h"
#include "console.h"
#include "fdt.h"
#include "timer.h"
#include "vm.h"
#include "mmu.h"
#include "pci.h"

#define RC_CFG_VENDOR_SPECIFIC_REG1 0x0188   // bits 3:2: BAR2 endianness
#define RC_CFG_PRIV1_ID_VAL3        0x043c   // the bridge's class code
#define MISC_MISC_CTRL              0x4008
#define MISC_CPU_2_PCIE_MEM_WIN0_LO 0x400c
#define MISC_CPU_2_PCIE_MEM_WIN0_HI 0x4010
#define MISC_RC_BAR1_CONFIG_LO      0x402c
#define MISC_RC_BAR2_CONFIG_LO      0x4034
#define MISC_RC_BAR2_CONFIG_HI      0x4038
#define MISC_RC_BAR3_CONFIG_LO      0x403c
#define MISC_PCIE_STATUS            0x4068
#define MISC_CPU_2_PCIE_MEM_WIN0_BASE_LIMIT 0x4070
#define MISC_CPU_2_PCIE_MEM_WIN0_BASE_HI    0x4080
#define MISC_CPU_2_PCIE_MEM_WIN0_LIMIT_HI   0x4084
#define MISC_HARD_PCIE_HARD_DEBUG   0x4204
#define MSI_INTR2_CLR               0x4508
#define MSI_INTR2_MASK_SET          0x4510
#define EXT_CFG_DATA                0x8000
#define EXT_CFG_INDEX               0x9000
#define RGR1_SW_INIT_1              0x9210

#define MISC_CTRL_SCB_ACCESS_EN     (1 << 12)
#define MISC_CTRL_CFG_READ_UR_MODE  (1 << 13)
#define MISC_CTRL_MAX_BURST_MASK    (3 << 20)   // 0: 128 bytes
#define MISC_CTRL_SCB0_SIZE_SHIFT   27
#define HARD_DEBUG_CLKREQ_DEBUG_ENABLE (1 << 1)
#define HARD_DEBUG_SERDES_IDDQ      (1 << 27)
#define STATUS_PHYLINKUP            (1 << 4)
#define STATUS_DL_ACTIVE            (1 << 5)
#define STATUS_PORT_RC              (1 << 7)
#define SW_INIT_PERST               (1 << 0)
#define SW_INIT_INIT                (1 << 1)

#define TAG_NOTIFY_XHCI_RESET 0x00030058
#define VL805_VENDOR 0x1106
#define VL805_DEVICE 0x3483

static uint64 base;

static void vl805(void);

static uint32 rd(int r) {
    return *(volatile uint32 *)(P2V(base) + r);
}

static void wr(int r, uint32 v) {
    *(volatile uint32 *)(P2V(base) + r) = v;
}

static void setbits(int r, uint32 mask, uint32 v) {
    wr(r, (rd(r) & ~mask) | v);
}

// Busy-wait us microseconds (boot: nothing to sleep with yet).
static void udelay(uint64 us) {
    for (uint64 t0 = timer_ns(); timer_ns() - t0 < us * 1000; )
        ;
}

static volatile uint32 *cfg(int bus, int dev, int fn, int off) {
    if (bus == 0)
        return (volatile uint32 *)(P2V(base) + off);
    wr(EXT_CFG_INDEX, bus << 20 | dev << 15 | fn << 12);
    return (volatile uint32 *)(P2V(base) + EXT_CFG_DATA + off);
}

static uint32 cfg_read(int bus, int dev, int fn, int off) {
    if (dev != 0)
        return 0xffffffff;   // one device per bus: the bridge, the VL805
    return *cfg(bus, dev, fn, off);
}

static void cfg_write(int bus, int dev, int fn, int off, uint32 v) {
    if (dev == 0)
        *cfg(bus, dev, fn, off) = v;
}

// the inbound window's size code, for a power of two size: 2^log2
static uint32 ibar_size(int log2) {
    if (log2 >= 12 && log2 <= 15)
        return log2 - 12 + 0x1c;
    if (log2 >= 16 && log2 <= 35)
        return log2 - 15;
    return 0;
}

int pcie_brcm_init(void) {
    int n = fdt_find_compatible(-1, "brcm,bcm2711-pcie");
    uint64 size;
    if (n < 0 || fdt_reg(n, 0, &base, &size) < 0 || pci_window(n) < 0)
        return -1;
    kvmmap(kernel_pagetable, P2V(base), base, size, PTE_KERN_DEVICE);

    // reset, the endpoint too (PERST#); out of reset, the SerDes on
    setbits(RGR1_SW_INIT_1, SW_INIT_INIT | SW_INIT_PERST, SW_INIT_INIT | SW_INIT_PERST);
    udelay(200);
    setbits(RGR1_SW_INIT_1, SW_INIT_INIT, 0);
    setbits(MISC_HARD_PCIE_HARD_DEBUG, HARD_DEBUG_SERDES_IDDQ, 0);
    udelay(200);

    // 128-byte bursts, and the inbound window: PCI address 0 is RAM's
    // 0, 4GB (dma-ranges: 3GB, as a power of two)
    setbits(MISC_MISC_CTRL, MISC_CTRL_SCB_ACCESS_EN | MISC_CTRL_CFG_READ_UR_MODE |
                            MISC_CTRL_MAX_BURST_MASK | 0x1f << MISC_CTRL_SCB0_SIZE_SHIFT,
            MISC_CTRL_SCB_ACCESS_EN | MISC_CTRL_CFG_READ_UR_MODE |
            (32 - 15) << MISC_CTRL_SCB0_SIZE_SHIFT);
    wr(MISC_RC_BAR2_CONFIG_LO, ibar_size(32));
    wr(MISC_RC_BAR2_CONFIG_HI, 0);
    setbits(MISC_RC_BAR1_CONFIG_LO, 0x1f, 0);   // the other windows off
    setbits(MISC_RC_BAR3_CONFIG_LO, 0x1f, 0);
    wr(MSI_INTR2_MASK_SET, 0xffffffff);         // no interrupts
    wr(MSI_INTR2_CLR, 0xffffffff);

    // the endpoint out of reset: 100ms for it (PCIe CEM spec), then
    // the link, within 100ms more
    setbits(RGR1_SW_INIT_1, SW_INIT_PERST, 0);
    udelay(100000);
    int up = 0;
    for (int t = 0; t < 20 && !up; t++) {
        udelay(5000);
        uint32 s = rd(MISC_PCIE_STATUS);
        up = (s & STATUS_PHYLINKUP) && (s & STATUS_DL_ACTIVE);
    }
    if (!up) {
        printf("pcie: the link didn't come up\n");
        return -1;
    }
    if (!(rd(MISC_PCIE_STATUS) & STATUS_PORT_RC)) {
        printf("pcie: not in root complex mode\n");
        return -1;
    }

    // the outbound window: CPU mem_cpu.. is PCI mem_pci..
    uint64 cpu_mb = pcihost.mem_cpu >> 20, limit_mb = (pcihost.mem_cpu + pcihost.mem_size - 1) >> 20;
    wr(MISC_CPU_2_PCIE_MEM_WIN0_LO, pcihost.mem_pci);
    wr(MISC_CPU_2_PCIE_MEM_WIN0_HI, pcihost.mem_pci >> 32);
    setbits(MISC_CPU_2_PCIE_MEM_WIN0_BASE_LIMIT, 0xfff00000 | 0xfff0,
            (limit_mb & 0xfff) << 20 | (cpu_mb & 0xfff) << 4);
    setbits(MISC_CPU_2_PCIE_MEM_WIN0_BASE_HI, 0xff, cpu_mb >> 12);
    setbits(MISC_CPU_2_PCIE_MEM_WIN0_LIMIT_HI, 0xff, limit_mb >> 12);

    // a PCI bridge, little-endian; the reference clock gated by CLKREQ#
    setbits(RC_CFG_PRIV1_ID_VAL3, 0xffffff, 0x060400);
    setbits(RC_CFG_VENDOR_SPECIFIC_REG1, 0xc, 0);
    setbits(MISC_HARD_PCIE_HARD_DEBUG, 0, HARD_DEBUG_CLKREQ_DEBUG_ENABLE);

    pcihost.read = cfg_read;
    pcihost.write = cfg_write;
    pcihost.onlydev0 = 1;
    pcihost.after_scan = vl805;
    printf("pcie: link up\n");
    return 0;
}

// The VL805's firmware: the VideoCore firmware loads it, after resetting
// the chip. Its BARs and command register are put back afterwards.
static void vl805(void) {
    struct pci_dev *d = pci_find(PCI_CLASS_XHCI);
    if (d == 0 || d->vendor != VL805_VENDOR || d->device != VL805_DEVICE)
        return;
    uint32 saved[6], cmd = pci_read(d, PCI_COMMAND);
    for (int i = 0; i < 6; i++)
        saved[i] = pci_read(d, PCI_BAR0 + 4 * i);
    uint32 m[7] = { sizeof(m), 0, TAG_NOTIFY_XHCI_RESET, 4, 0,
                    d->bus << 20 | d->dev << 15 | d->fn << 12, 0 };
    if (rpi_mbox_call(m, NELEM(m)) < 0)
        printf("pcie: the firmware didn't load the VL805's firmware\n");
    udelay(1000);
    for (int i = 0; i < 6; i++)
        pci_write(d, PCI_BAR0 + 4 * i, saved[i]);
    pci_write(d, PCI_COMMAND, cmd);
}
