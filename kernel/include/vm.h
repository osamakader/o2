#ifndef VM_H
#define VM_H

#include "types.h"

// Page-table geometry shared by arm64 (4KB granule, 39-bit VA) and
// riscv64 Sv39: three levels of 512 entries, 9 VA bits per level.
// Level 2 is the root, level 0 holds the leaf page entries.

#define PGSHIFT 12
#define PXMASK  0x1FF
#define PXSHIFT(level) (PGSHIFT + (9 * (level)))
#define PX(level, va)  ((((uint64)(va)) >> PXSHIFT(level)) & PXMASK)

// one beyond the highest user (low half) virtual address
#define MAXVA (1UL << 39)

// arch-neutral permissions for user mappings; readable is implied.
// The arch turns them into descriptor bits with PTE_USER(perm).
#define PERM_W 0x1
#define PERM_X 0x2

typedef uint64 pte_t;
typedef uint64 *pagetable_t;   // 512 PTEs

pte_t *walk(pagetable_t pagetable, uint64 va, int alloc);
int mappages(pagetable_t pagetable, uint64 va, uint64 size, uint64 pa, uint64 attrs);
void kvminit(void);
void kvminithart(void);   // arch: switch this core to kernel_pagetable
void uvmswitch(pagetable_t pagetable);   // arch: install a user page table

extern pagetable_t kernel_pagetable;

#endif
