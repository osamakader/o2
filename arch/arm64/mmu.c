#include "types.h"
#include "memlayout.h"
#include "vm.h"
#include "mmu.h"
#include "proc.h"
#include "param.h"
#include "defs.h"
#include "console.h"
#include "board.h"

// Map the devices the kernel drives: UART, RTC, GIC, virtio.
void kvmmap_devices(pagetable_t pagetable) {
    board_map(pagetable);
}

// An all-invalid table for TTBR0 when no user page table is
// installed, so stray kernel accesses to low addresses still fault.
static pagetable_t empty_pagetable;

// KPTI: while user code runs, TTBR1 holds tramp_pagetable, which maps
// only the trampoline page (tramp.S) and tramp_data, at the same
// addresses as kernel_pagetable does. tramp_data gives the trampoline
// the two TTBR1 values and the kernel TTBR0; it sits alone in its page.
extern char tramp_vectors[];
__attribute__((aligned(PGSIZE))) uint64 tramp_data[PGSIZE / sizeof(uint64)];
static pagetable_t tramp_pagetable;

static void tramp_init(void) {
    tramp_pagetable = kalloc();
    if (tramp_pagetable == 0)
        panic("tramp_init");
    memset(tramp_pagetable, 0, PGSIZE);

    if (mappages(tramp_pagetable, (uint64)tramp_vectors, PGSIZE,
                 V2P(tramp_vectors), PTE_KERN_TEXT) != 0 ||
        mappages(tramp_pagetable, (uint64)tramp_data, PGSIZE,
                 V2P(tramp_data), PTE_KERN_RODATA) != 0)
        panic("tramp_init: map");
#ifdef EARLY_UART
    // the UART too, for tramp_hang (tramp.S), on a board being brought up
    if (mappages(tramp_pagetable, P2V(EARLY_UART & ~0xfffUL), PGSIZE, EARLY_UART & ~0xfffUL,
                 PTE_KERN_DEVICE) != 0)
        panic("tramp_init: map uart");
#endif

    tramp_data[0] = V2P(kernel_pagetable);  // TTBR1 in the kernel
    tramp_data[1] = V2P(tramp_pagetable);   // TTBR1 in user mode
    tramp_data[2] = V2P(empty_pagetable);   // TTBR0 in the kernel, ASID 0
}


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

    if (tramp_pagetable == 0)
        tramp_init();

    asm volatile("msr ttbr1_el1, %0" : : "r"(V2P(kernel_pagetable)));
    asm volatile("msr ttbr0_el1, %0" : : "r"(V2P(empty_pagetable)));

    // make the page-table writes visible to the walker, then drop the
    // boot table's translations.
    asm volatile("dsb ishst; tlbi vmalle1; dsb ish; isb" ::: "memory");
}

// Stage a user page table and ASID for tramp_exit (tramp.S), which
// loads them into TTBR0 once TTBR1 holds the trampoline table. The
// kernel itself always runs with TTBR0 = empty_pagetable, ASID 0, so
// its translations are never tagged with a user ASID. TPIDRRO_EL0 is
// the hand-over register; tramp_exit clears it before eret. Call with
// interrupts off, right before returning to EL0.
void uvmswitch(pagetable_t pagetable, int asid) {
#ifndef ARM64_ASID
    asid = 0;  // like the kernel: tramp_exit flushes instead
#endif
    asm volatile("msr tpidrro_el0, %0" : : "r"(V2P(pagetable) | TTBR_ASID(asid)));
}

// The instruction cache is not coherent with data writes: after the
// kernel writes code (e.g. loading a program), clean those lines to
// the point of unification and invalidate the instruction caches.
// va is a kernel address of the written bytes; the caches are
// physically indexed, so the user alias sees the result too.
// Write the data cache line holding va back to memory and drop it, so
// that a device reading or writing the memory behind the caches (the
// Raspberry Pi's GPU) and the CPU agree on it.
void dcache_flush_line(uint64 va) {
    asm volatile("dc civac, %0; dsb sy" : : "r"(va) : "memory");
}

void sync_icache(uint64 va, uint64 len) {
    uint64 ctr;
    asm volatile("mrs %0, ctr_el0" : "=r"(ctr));
    uint64 line = 4UL << ((ctr >> 16) & 0xf);   // DminLine, in bytes

    for (uint64 a = va & ~(line - 1); a < va + len; a += line)
        asm volatile("dc cvau, %0" : : "r"(a) : "memory");
    asm volatile("dsb ish; ic ialluis; dsb ish; isb" ::: "memory");
}

// The kernel always runs with TTBR0 = empty_pagetable (tramp.S switches
// on every entry), so there is no user address space to leave.
void kvmswitch(void) {
}

// A user page table: nothing beyond the user's own memory yet.
pagetable_t proc_pagetable(struct proc *p) {
    return uvmcreate();
}

void proc_freepagetable(pagetable_t pagetable, uint64 sz) {
    uvmfree(pagetable, sz);
}

// Traps from user mode save registers at the top of the (otherwise
// empty) kernel stack.
struct trapframe *trapframe_alloc(uint64 kstack) {
    return (struct trapframe *)(kstack + PGSIZE) - 1;
}

// Drop every CPU's translations for asid (inner-shareable broadcast).
// Without ARM64_ASID everything runs under ASID 0 and every return to
// user mode flushes, so there's nothing to drop.
void asid_flush(int asid) {
#ifdef ARM64_ASID
    asm volatile("dsb ishst; tlbi aside1is, %0; dsb ish; isb"
                 : : "r"(TTBR_ASID(asid)) : "memory");
#endif
}

_Static_assert(NPROC < 256, "ASIDs are slot + 1 and arm64 has 8 bits");

// Map the DMA alias of the page at pa (see DMA_OFFSET), uncached.
void dma_map(uint64 pa) {
    kvmmap(kernel_pagetable, pa + DMA_OFFSET, pa, PGSIZE, PTE_KERN_NC);
}

// New kernel page-table entries: visible to the walker. (Entries that
// were invalid aren't in the TLB: no invalidation.)
void kvmsync(void) {
    asm volatile("dsb ishst; isb" ::: "memory");
}
