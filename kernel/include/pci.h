#ifndef PCI_H
#define PCI_H

#include "types.h"

// config space registers
#define PCI_ID      0x00   // vendor, device
#define PCI_COMMAND 0x04
#define PCI_CLASS   0x08   // revision, then the class in bits 31:8
#define PCI_HEADER  0x0c   // header type in bits 23:16 (bit 23: multi-function)
#define PCI_BAR0    0x10
#define PCI_BUSES   0x18   // bridge: primary, secondary, subordinate bus
#define PCI_MEMWIN  0x20   // bridge: memory base and limit, bits 31:20 of each

#define PCI_CMD_MEM    0x2
#define PCI_CMD_MASTER 0x4

#define PCI_CLASS_XHCI 0x0c0330

// the host bridge: config space access and its memory window
struct pci_host {
    uint32 (*read)(int bus, int dev, int fn, int off);
    void (*write)(int bus, int dev, int fn, int off, uint32 v);
    uint64 mem_pci, mem_cpu, mem_size;   // the window, as PCI and CPU see it
    uint64 next;                         // PCI address free from here
    int onlydev0;                        // only device 0 on each bus
};

#define NPCIDEV 16

struct pci_dev {
    int bus, dev, fn;
    uint16 vendor, device;
    uint32 class;
    uint64 bar[6];       // CPU addresses of the memory BARs, or 0
    uint64 barsize[6];
};

extern struct pci_host pcihost;

void pci_init(void);
int pci_window(int node);
struct pci_dev *pci_find(uint32 class);
void pci_enable(struct pci_dev *d);
uint32 pci_read(struct pci_dev *d, int off);
void pci_write(struct pci_dev *d, int off, uint32 v);

#endif
