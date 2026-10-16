#include "types.h"
#include "memlayout.h"
#include "vm.h"
#include "mmu.h"

// Turn on the MMU on this core with kernel_pagetable in TTBR0, and
// enable the caches. Runs with the kernel identity mapped, so the
// next instruction fetch after SCTLR_EL1.M is set still resolves.
void kvminithart(void) {
    uint64 mmfr0, sctlr;

    // physical address size this core supports, capped at 48 bits
    asm volatile("mrs %0, id_aa64mmfr0_el1" : "=r"(mmfr0));
    uint64 ips = mmfr0 & 0xf;
    if (ips > 5)
        ips = 5;

    asm volatile("msr mair_el1, %0" : : "r"(MAIR_VALUE));
    asm volatile("msr tcr_el1, %0" : : "r"(TCR_VALUE | (ips << TCR_IPS_SHIFT)));
    asm volatile("msr ttbr0_el1, %0" : : "r"(V2P(kernel_pagetable)));

    // make the page-table writes visible to the walker, drop any
    // stale translations, then switch translation on.
    asm volatile("dsb ish; tlbi vmalle1; dsb ish; isb" ::: "memory");

    asm volatile("mrs %0, sctlr_el1" : "=r"(sctlr));
    sctlr |= SCTLR_M | SCTLR_C | SCTLR_I;
    asm volatile("msr sctlr_el1, %0; isb" : : "r"(sctlr) : "memory");
}
