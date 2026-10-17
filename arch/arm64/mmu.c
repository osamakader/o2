#include "types.h"
#include "memlayout.h"
#include "vm.h"
#include "mmu.h"
#include "defs.h"
#include "console.h"

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
