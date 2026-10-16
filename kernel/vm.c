#include "types.h"
#include "memlayout.h"
#include "defs.h"
#include "console.h"
#include "vm.h"
#include "mmu.h"

// the kernel's page table, an identity map of RAM and devices.
pagetable_t kernel_pagetable;

// Return the address of the leaf PTE for va, allocating the
// intermediate tables if alloc is set. Returns 0 if a table is
// missing and alloc is clear, or if allocation fails.
pte_t *walk(pagetable_t pagetable, uint64 va, int alloc) {
    if (va >= MAXVA)
        panic("walk");

    for (int level = 2; level > 0; level--) {
        pte_t *pte = &pagetable[PX(level, va)];
        if (*pte & PTE_VALID) {
            pagetable = (pagetable_t)PTE2PA(*pte);
        } else {
            if (!alloc || (pagetable = (pagetable_t)kalloc()) == 0)
                return 0;
            memset(pagetable, 0, PGSIZE);
            *pte = MKTABLE(pagetable);
        }
    }
    return &pagetable[PX(0, va)];
}

// Map [va, va+size) to physical addresses starting at pa. va and size
// need not be page aligned. Returns 0 on success, -1 if walk() could
// not allocate a page-table page.
int mappages(pagetable_t pagetable, uint64 va, uint64 size, uint64 pa, uint64 attrs) {
    if (size == 0)
        panic("mappages: size");

    uint64 a = PGROUNDDOWN(va);
    uint64 last = PGROUNDDOWN(va + size - 1);
    for (;;) {
        pte_t *pte = walk(pagetable, a, 1);
        if (pte == 0)
            return -1;
        if (*pte & PTE_VALID)
            panic("mappages: remap");
        *pte = MKPAGE(pa, attrs);
        if (a == last)
            break;
        a += PGSIZE;
        pa += PGSIZE;
    }
    return 0;
}

static void kvmmap(pagetable_t pagetable, uint64 va, uint64 pa, uint64 size, uint64 attrs) {
    if (mappages(pagetable, va, size, pa, attrs) != 0)
        panic("kvmmap");
}

static pagetable_t kvmmake(void) {
    pagetable_t pagetable = (pagetable_t)kalloc();
    if (!pagetable)
        panic("kvmmake");
    memset(pagetable, 0, PGSIZE);

    kvmmap(pagetable, UART0, UART0, PGSIZE, PTE_KERN_DEVICE);
    kvmmap(pagetable, GICD, GICD, GICD_SIZE, PTE_KERN_DEVICE);
    kvmmap(pagetable, GICR, GICR, GICR_SIZE, PTE_KERN_DEVICE);

    // all of RAM, including the kernel image
    kvmmap(pagetable, KERNBASE, KERNBASE, PHYSTOP - KERNBASE, PTE_KERN_RWX);

    return pagetable;
}

void kvminit(void) {
    kernel_pagetable = kvmmake();
}
