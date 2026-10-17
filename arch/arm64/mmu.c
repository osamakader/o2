#include "types.h"
#include "memlayout.h"
#include "vm.h"
#include "mmu.h"
#include "defs.h"
#include "console.h"

// Map the qemu virt devices the kernel drives: UART, GIC.
void kvmmap_devices(pagetable_t pagetable) {
    kvmmap(pagetable, P2V(UART0), UART0, PGSIZE, PTE_KERN_DEVICE);
    kvmmap(pagetable, P2V(GICD), GICD, GICD_SIZE, PTE_KERN_DEVICE);
    kvmmap(pagetable, P2V(GICR), GICR, GICR_SIZE, PTE_KERN_DEVICE);
}

// An all-invalid table for TTBR0 when no user page table is
// installed, so stray kernel accesses to low addresses still fault.
static pagetable_t empty_pagetable;

// Switch this core from the boot page table (entry.S) to
// kernel_pagetable in TTBR1. The MMU, MAIR and TCR are already set up.
// The low half (TTBR0) is for user page tables; until uvmswitch()
// installs one it points at an empty table.
void kvminithart(void) {
    if (empty_pagetable == 0) {
        empty_pagetable = kalloc();
        if (empty_pagetable == 0)
            panic("kvminithart");
        memset(empty_pagetable, 0, PGSIZE);
    }

    asm volatile("msr ttbr1_el1, %0" : : "r"(V2P(kernel_pagetable)));
    asm volatile("msr ttbr0_el1, %0" : : "r"(V2P(empty_pagetable)));

    // make the page-table writes visible to the walker, then drop the
    // boot table's translations.
    asm volatile("dsb ishst; tlbi vmalle1; dsb ish; isb" ::: "memory");
}

// Install a user page table in TTBR0 on this core. There are no ASIDs
// yet, so drop all of this core's translations: entries from the
// previous process, or from pages since unmapped from this one.
// Call with interrupts off, right before returning to EL0.
void uvmswitch(pagetable_t pagetable) {
    asm volatile("msr ttbr0_el1, %0" : : "r"(V2P(pagetable)));
    asm volatile("isb; tlbi vmalle1; dsb nsh; isb" ::: "memory");
}

// The instruction cache is not coherent with data writes: after the
// kernel writes code (e.g. loading a program), clean those lines to
// the point of unification and invalidate the instruction caches.
// va is a kernel address of the written bytes; the caches are
// physically indexed, so the user alias sees the result too.
void sync_icache(uint64 va, uint64 len) {
    uint64 ctr;
    asm volatile("mrs %0, ctr_el0" : "=r"(ctr));
    uint64 line = 4UL << ((ctr >> 16) & 0xf);   // DminLine, in bytes

    for (uint64 a = va & ~(line - 1); a < va + len; a += line)
        asm volatile("dc cvau, %0" : : "r"(a) : "memory");
    asm volatile("dsb ish; ic ialluis; dsb ish; isb" ::: "memory");
}

// Leave a user address space: point TTBR0 back at the empty table so
// a page table about to be freed is never walked, even speculatively.
// No TLB flush needed: uvmswitch() flushes before user mode runs again.
void kvmswitch(void) {
    asm volatile("msr ttbr0_el1, %0; isb" : : "r"(V2P(empty_pagetable)) : "memory");
}
