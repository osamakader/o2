#ifndef MMU_H
#define MMU_H

// arm64 VMSAv8-64 stage 1 descriptors, 4KB granule, 39-bit VA
// (T0SZ = 25): translation starts at level 1, three levels in total.

#define PTE_VALID      (1UL << 0)
#define PTE_TABLE      (1UL << 1)   // table (levels 1-2) or page (level 3)
#define PTE_ATTRIDX(i) ((uint64)(i) << 2)
#define PTE_AP_RW      (0UL << 6)   // EL1 read/write, EL0 none
#define PTE_AP_RW_USER (1UL << 6)   // EL1 and EL0 read/write
#define PTE_AP_RO      (2UL << 6)   // EL1 read-only, EL0 none
#define PTE_AP_RO_USER (3UL << 6)   // EL1 and EL0 read-only
#define PTE_SH_INNER   (3UL << 8)
#define PTE_AF         (1UL << 10)  // access flag; unset would fault
#define PTE_PXN        (1UL << 53)  // no execute at EL1
#define PTE_UXN        (1UL << 54)  // no execute at EL0

// MAIR_EL1 attribute slots
#define MT_DEVICE_nGnRE 0
#define MT_NORMAL       1
#define MAIR_VALUE      ((0x04UL << (8 * MT_DEVICE_nGnRE)) | \
                         (0xffUL << (8 * MT_NORMAL)))

// output address bits [47:12] sit in place in the descriptor
#define PTE_ADDR_MASK  0x0000fffffffff000UL
#define PA2PTE(pa)     ((uint64)(pa) & PTE_ADDR_MASK)
#define PTE2PA(pte)    ((uint64)(pte) & PTE_ADDR_MASK)

// descriptor pointing at the next-level table
#define MKTABLE(pa)    (PA2PTE(pa) | PTE_TABLE | PTE_VALID)
// level 3 page descriptor
#define MKPAGE(pa, attrs) (PA2PTE(pa) | (attrs) | PTE_AF | PTE_TABLE | PTE_VALID)

// mapping attributes for kernel mappings
#define PTE_KERN_RWX    (PTE_ATTRIDX(MT_NORMAL) | PTE_SH_INNER | PTE_AP_RW | PTE_UXN)
#define PTE_KERN_DEVICE (PTE_ATTRIDX(MT_DEVICE_nGnRE) | PTE_AP_RW | PTE_PXN | PTE_UXN)

#endif
