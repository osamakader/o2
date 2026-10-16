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

// one beyond the highest virtual address
#define MAXVA (1UL << 39)

typedef uint64 pte_t;
typedef uint64 *pagetable_t;   // 512 PTEs

pte_t *walk(pagetable_t pagetable, uint64 va, int alloc);
int mappages(pagetable_t pagetable, uint64 va, uint64 size, uint64 pa, uint64 attrs);
void kvminit(void);
void kvminithart(void);   // arch: turn on paging with kernel_pagetable

extern pagetable_t kernel_pagetable;

#endif
