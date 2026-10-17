#ifndef MMU_H
#define MMU_H

// arm64 VMSAv8-64 stage 1 descriptors, 4KB granule, 39-bit VA
// (T0SZ = 25): translation starts at level 1, three levels in total.
//
// Included from assembly too: constants use _UL() instead of a UL
// suffix, and C-only macros (with casts) are never expanded there.

#ifdef __ASSEMBLER__
#define _UL(x) x
#else
#define _UL(x) x##UL
#endif

#define PTE_VALID      (_UL(1) << 0)
#define PTE_TABLE      (_UL(1) << 1)   // table (levels 1-2) or page (level 3)
#define PTE_ATTRIDX(i) (_UL(i) << 2)
#define PTE_AP_RW      (_UL(0) << 6)   // EL1 read/write, EL0 none
#define PTE_AP_RW_USER (_UL(1) << 6)   // EL1 and EL0 read/write
#define PTE_AP_RO      (_UL(2) << 6)   // EL1 read-only, EL0 none
#define PTE_AP_RO_USER (_UL(3) << 6)   // EL1 and EL0 read-only
#define PTE_SH_INNER   (_UL(3) << 8)
#define PTE_AF         (_UL(1) << 10)  // access flag; unset would fault
#define PTE_NG         (_UL(1) << 11)  // not global: tagged with the ASID
#define PTE_PXN        (_UL(1) << 53)  // no execute at EL1
#define PTE_UXN        (_UL(1) << 54)  // no execute at EL0

// MAIR_EL1 attribute slots
#define MT_DEVICE_nGnRE 0
#define MT_NORMAL       1
#define MAIR_VALUE      ((_UL(0x04) << (8 * MT_DEVICE_nGnRE)) | \
                         (_UL(0xff) << (8 * MT_NORMAL)))

// TCR_EL1: 39-bit VA in both halves, 4KB granule, inner-shareable
// write-back cacheable table walks. TTBR0 holds the low half (boot
// identity map, later user space), TTBR1 the kernel's high half.
#define TCR_T0SZ(n)     (_UL(n) << 0)
#define TCR_EPD0        (_UL(1) << 7)   // disable TTBR0 walks
#define TCR_IRGN0_WBWA  (_UL(1) << 8)
#define TCR_ORGN0_WBWA  (_UL(1) << 10)
#define TCR_SH0_INNER   (_UL(3) << 12)
#define TCR_TG0_4K      (_UL(0) << 14)
#define TCR_T1SZ(n)     (_UL(n) << 16)
#define TCR_IRGN1_WBWA  (_UL(1) << 24)
#define TCR_ORGN1_WBWA  (_UL(1) << 26)
#define TCR_SH1_INNER   (_UL(3) << 28)
#define TCR_TG1_4K      (_UL(2) << 30)
#define TCR_IPS_SHIFT   32
#define TCR_VALUE       (TCR_T0SZ(25) | TCR_IRGN0_WBWA | TCR_ORGN0_WBWA | \
                         TCR_SH0_INNER | TCR_TG0_4K | \
                         TCR_T1SZ(25) | TCR_IRGN1_WBWA | TCR_ORGN1_WBWA | \
                         TCR_SH1_INNER | TCR_TG1_4K)

// With T0SZ = T1SZ = 25, a VA is translated if bits [63:39] are all 0
// (TTBR0) or all 1 (TTBR1).
#define VA_VALID(va)    (((va) >> 39) == 0 || ((va) >> 39) == (~_UL(0) >> 39))

// User space is the whole TTBR0 range, [0, 2^39); the kernel lives in
// TTBR1, so user root tables hold no kernel entries.
#define MAXVA            (_UL(1) << 39)
#define USERTOP          MAXVA   // end of user memory
#define UVM_ROOT_ENTRIES 512
#ifndef __ASSEMBLER__
static inline void uvminit_root(uint64 *root) { }
void uvmswitch(uint64 *pagetable, int asid);
#endif

// SCTLR_EL1 bits
#define SCTLR_M (_UL(1) << 0)    // MMU enable
#define SCTLR_C (_UL(1) << 2)    // data cache enable
#define SCTLR_I (_UL(1) << 12)   // instruction cache enable

// TTBR0_EL1.ASID (TCR_EL1.A1 = 0: TTBR0 supplies the ASID; AS = 0: 8
// bits)
#define TTBR_ASID(asid) ((uint64)(asid) << 48)

// output address bits [47:12] sit in place in the descriptor
#define PTE_ADDR_MASK  _UL(0x0000fffffffff000)
#define PA2PTE(pa)     ((uint64)(pa) & PTE_ADDR_MASK)
#define PTE2PA(pte)    ((uint64)(pte) & PTE_ADDR_MASK)
#define PTE_FLAGS(pte) ((uint64)(pte) & ~PTE_ADDR_MASK)

// descriptor pointing at the next-level table
#define MKTABLE(pa)    (PA2PTE(pa) | PTE_TABLE | PTE_VALID)
// level 3 page descriptor
#define MKPAGE(pa, attrs) (PA2PTE(pa) | (attrs) | PTE_AF | PTE_TABLE | PTE_VALID)

// mapping attributes for kernel mappings
// All mappings are non-global (nG), so the TLB tags them with the ASID
// in TTBR0: 0 while the kernel runs, the process's in user mode. A
// global kernel entry would stay usable from EL0.
#define PTE_KERN_TEXT   (PTE_ATTRIDX(MT_NORMAL) | PTE_SH_INNER | PTE_AP_RO | PTE_UXN | PTE_NG)
#define PTE_KERN_RODATA (PTE_ATTRIDX(MT_NORMAL) | PTE_SH_INNER | PTE_AP_RO | PTE_PXN | PTE_UXN | PTE_NG)
#define PTE_KERN_DATA   (PTE_ATTRIDX(MT_NORMAL) | PTE_SH_INNER | PTE_AP_RW | PTE_PXN | PTE_UXN | PTE_NG)
#define PTE_KERN_DEVICE (PTE_ATTRIDX(MT_DEVICE_nGnRE) | PTE_AP_RW | PTE_PXN | PTE_UXN | PTE_NG)

// user mappings: never executable at EL1; read-only unless PERM_W,
// not executable at EL0 unless PERM_X.
#define PTE_USER(perm) (PTE_ATTRIDX(MT_NORMAL) | PTE_SH_INNER | PTE_PXN | PTE_NG | \
                        (((perm) & PERM_W) ? PTE_AP_RW_USER : PTE_AP_RO_USER) | \
                        (((perm) & PERM_X) ? 0 : PTE_UXN))

// is this leaf accessible / writable from EL0? (AP[2:1] = x1 / 01)
#define PTE_AP_MASK      (_UL(3) << 6)
#define PTE_IS_USER(pte)   (((pte) & PTE_AP_RW_USER) != 0)
#define PTE_IS_USER_W(pte) (((pte) & PTE_AP_MASK) == PTE_AP_RW_USER)
// make a user leaf EL1-only (AP = 00), e.g. for the stack guard page
#define PTE_CLEAR_USER(pte) ((pte) & ~PTE_AP_MASK)

#endif
