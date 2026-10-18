#ifndef MMU_H
#define MMU_H

// riscv64 Sv39 page-table entries: three levels, 9 VA bits each.
// Plain constants are usable from assembly (via _UL()); macros with
// casts are C-only.

#ifdef __ASSEMBLER__
#define _UL(x) x
#else
#define _UL(x) x##UL
#endif

#define PTE_VALID (_UL(1) << 0)
#define PTE_R     (_UL(1) << 1)
#define PTE_W     (_UL(1) << 2)
#define PTE_X     (_UL(1) << 3)
#define PTE_U     (_UL(1) << 4)   // user-accessible
#define PTE_G     (_UL(1) << 5)   // global
#define PTE_A     (_UL(1) << 6)   // accessed
#define PTE_D     (_UL(1) << 7)   // dirty

// PPN in bits [53:10]
#define PA2PTE(pa)     ((((uint64)(pa)) >> 12) << 10)
#define PTE2PA(pte)    ((((uint64)(pte)) >> 10) << 12)
#define PTE_FLAGS(pte) ((uint64)(pte) & 0x3FF)

// A non-leaf entry has R = W = X = 0.
#define MKTABLE(pa)       (PA2PTE(pa) | PTE_VALID)
// Leaf entries get A and D up front: hardware may fault instead of
// setting them.
#define MKPAGE(pa, attrs) (PA2PTE(pa) | (attrs) | PTE_A | PTE_D | PTE_VALID)

// Sv39 has no memory types; device vs memory comes from the PMAs.
// Kernel mappings are not global (G): global translations match every
// ASID, which would make them usable while user code runs. They are
// tagged with the kernel's ASID 0 instead.
#define PTE_KERN_TEXT   (PTE_R | PTE_X)
#define PTE_KERN_RODATA (PTE_R)
#define PTE_KERN_DATA   (PTE_R | PTE_W)
#define PTE_KERN_DEVICE (PTE_R | PTE_W)

// user mappings: readable; writable/executable per PERM_W/PERM_X.
#define PTE_USER(perm) (PTE_U | PTE_R | \
                        (((perm) & PERM_W) ? PTE_W : 0) | \
                        (((perm) & PERM_X) ? PTE_X : 0))
#define PTE_IS_USER(pte)    (((pte) & PTE_U) != 0)
#define PTE_IS_USER_W(pte)  (((pte) & (PTE_U | PTE_W)) == (PTE_U | PTE_W))
#define PTE_CLEAR_USER(pte) ((pte) & ~PTE_U)

// copy-on-write (see uvmcopy()): a page shared read-only after fork()
// that the process may write, marked with a bit reserved for software
#define PTE_COW            (_UL(1) << 8)
#define PTE_IS_COW(pte)    (((pte) & PTE_COW) != 0)
#define PTE_MAKE_COW(pte)  (((pte) & ~PTE_W) | PTE_COW)
#define PTE_UNCOW(pte)     (((pte) & ~PTE_COW) | PTE_W)

// Sv39: bits [63:39] must equal bit 38. The lower half is user space,
// the upper half the kernel's.
#define VA_VALID(va)  ((((long)(va)) << 25 >> 25) == (long)(va))
#define MAXVA            (_UL(1) << 38)

// The top two pages of the user half, as in xv6: the trampoline
// (uservec/userret, mapped at the same address in the kernel page
// table) and the process's trap frame. Neither is user-accessible.
// User page tables map nothing else of the kernel.
#define TRAMPOLINE       (MAXVA - 0x1000)
#define TRAPFRAME        (TRAMPOLINE - 0x1000)
#define USERTOP          TRAPFRAME   // end of user memory

#define UVM_ROOT_ENTRIES 512
#ifndef __ASSEMBLER__
static inline void uvminit_root(uint64 *root) { }
#endif

// satp: Sv39 mode, address-space ID and the root table's PPN
#define SATP_SV39 (_UL(8) << 60)
#define SATP_ASID_SHIFT 44
#define SATP_ASID_MASK  (_UL(0xffff) << SATP_ASID_SHIFT)
#define MAKE_SATP(pa) (SATP_SV39 | (((uint64)(pa)) >> 12))
#define SATP_ASID(asid) ((uint64)(asid) << SATP_ASID_SHIFT)

#endif
