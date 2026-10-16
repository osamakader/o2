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

// TCR_EL1: 39-bit VA, 4KB granule, inner-shareable write-back
// cacheable table walks. TTBR1 walks are disabled (EPD1) for now.
#define TCR_T0SZ(n)     ((uint64)(n) << 0)
#define TCR_IRGN0_WBWA  (1UL << 8)
#define TCR_ORGN0_WBWA  (1UL << 10)
#define TCR_SH0_INNER   (3UL << 12)
#define TCR_TG0_4K      (0UL << 14)
#define TCR_T1SZ(n)     ((uint64)(n) << 16)
#define TCR_EPD1        (1UL << 23)
#define TCR_TG1_4K      (2UL << 30)
#define TCR_IPS_SHIFT   32
#define TCR_VALUE       (TCR_T0SZ(25) | TCR_IRGN0_WBWA | TCR_ORGN0_WBWA | \
                         TCR_SH0_INNER | TCR_TG0_4K | TCR_T1SZ(25) | \
                         TCR_EPD1 | TCR_TG1_4K)

// SCTLR_EL1 bits
#define SCTLR_M (1UL << 0)    // MMU enable
#define SCTLR_C (1UL << 2)    // data cache enable
#define SCTLR_I (1UL << 12)   // instruction cache enable

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
