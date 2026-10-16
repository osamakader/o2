#include "types.h"
#include "memlayout.h"
#include "vm.h"
#include "mmu.h"

// Switch this core from the boot page table (entry.S) to
// kernel_pagetable in TTBR1. The MMU, MAIR and TCR are already set up.
// Nothing runs from the low half any more, so stop TTBR0 walks until
// there are user page tables to put there.
void kvminithart(void) {
    uint64 tcr;

    asm volatile("msr ttbr1_el1, %0" : : "r"(V2P(kernel_pagetable)));

    asm volatile("mrs %0, tcr_el1" : "=r"(tcr));
    tcr |= TCR_EPD0;
    asm volatile("msr tcr_el1, %0" : : "r"(tcr));

    // make the page-table writes visible to the walker, then drop the
    // boot table's translations.
    asm volatile("dsb ishst; tlbi vmalle1; dsb ish; isb" ::: "memory");
}
