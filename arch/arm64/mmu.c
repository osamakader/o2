#include "types.h"
#include "memlayout.h"
#include "vm.h"
#include "mmu.h"

// Switch this core from the boot page table (entry.S) to
// kernel_pagetable. The MMU, MAIR and TCR are already set up.
void kvminithart(void) {
    asm volatile("msr ttbr0_el1, %0" : : "r"(V2P(kernel_pagetable)));

    // make the page-table writes visible to the walker, then drop the
    // boot table's translations.
    asm volatile("dsb ishst; tlbi vmalle1; dsb ish; isb" ::: "memory");
}
