#include "types.h"
#include "memlayout.h"
#include "board.h"
#include "vm.h"
#include "mmu.h"
#include "proc.h"
#include "cpu.h"
#include "defs.h"
#include "console.h"
#include "param.h"
#include "sbi.h"

extern char trampoline[];  // trampoline.S

// Map the devices the kernel drives (UART, RTC, PLIC, virtio) and the
// trampoline.
void kvmmap_devices(pagetable_t pagetable) {
    board_map(pagetable);
    kvmmap(pagetable, TRAMPOLINE, V2P(trampoline), PGSIZE, PTE_R | PTE_X);
}

// Do user address spaces get their own ASIDs? Only if the hardware
// has enough ASID bits for every ptable slot; otherwise every user
// address space uses ASID 0 too, and each return to user mode flushes
// the TLB (tramp_userret).
int asids;

// How many ASID bits does this hart implement? Write all ones to
// satp.ASID and see which stick (kernel_pagetable stays installed).
static int asid_bits(void) {
    csr_write(satp, MAKE_SATP(V2P(kernel_pagetable)) | SATP_ASID_MASK);
    uint64 asid = (csr_read(satp) & SATP_ASID_MASK) >> SATP_ASID_SHIFT;
    int bits = 0;
    while (asid & (1UL << bits))
        bits++;
    return bits;
}

// Switch this hart from the boot page table (entry.S) to
// kernel_pagetable, with the kernel's ASID 0.
void kvminithart(void) {
    // wait for any previous writes to the page table memory to finish.
    asm volatile("sfence.vma zero, zero");

    static int checked;
    if (!checked) {
        int bits = asid_bits();
        asids = (1 << bits) > NPROC;
        checked = 1;
        printf("riscv64: %d ASID bits, %s\n", bits,
               asids ? "per-process ASIDs" : "flushing on every return to user mode");
    }

    csr_write(satp, MAKE_SATP(V2P(kernel_pagetable)));

    // flush stale entries from the TLB, the boot table's global ones too.
    asm volatile("sfence.vma zero, zero");
}

// Make code the kernel wrote visible to instruction fetch. fence.i
// only covers this hart, so usertrapret() repeats it on every return to
// user mode, wherever the process runs.
// Caches are coherent with devices on qemu: nothing to do.
void dcache_flush_line(uint64 va) {
}

void sync_icache(uint64 va, uint64 len) {
    asm volatile("fence.i" ::: "memory");
}

// The kernel always runs on kernel_pagetable (uservec switches to it),
// so there is no user address space to leave.
void kvmswitch(void) {
}


// A user page table mapping, besides the user's memory, only the
// trampoline and p's trap frame page; neither is user-accessible.
pagetable_t proc_pagetable(struct proc *p) {
    pagetable_t pagetable = uvmcreate();
    if (pagetable == 0)
        return 0;

    if (mappages(pagetable, TRAMPOLINE, PGSIZE, V2P(trampoline), PTE_R | PTE_X) < 0) {
        uvmfree(pagetable, 0);
        return 0;
    }
    if (mappages(pagetable, TRAPFRAME, PGSIZE, V2P(p->trapframe), PTE_R | PTE_W) < 0) {
        uvmunmap(pagetable, TRAMPOLINE, 1, 0);
        uvmfree(pagetable, 0);
        return 0;
    }
    return pagetable;
}

// Free a user page table and the memory it maps; the trampoline and
// trap frame pages aren't the process's to free.
void proc_freepagetable(pagetable_t pagetable, uint64 sz) {
    uvmunmap(pagetable, TRAMPOLINE, 1, 0);
    uvmunmap(pagetable, TRAPFRAME, 1, 0);
    uvmfree(pagetable, sz);
}

// Each ptable slot gets its own trap frame page, mapped at TRAPFRAME in
// its process's page tables.
struct trapframe *trapframe_alloc(uint64 kstack) {
    struct trapframe *tf = kalloc();
    if (tf == 0)
        panic("trapframe_alloc");
    memset(tf, 0, PGSIZE);
    return tf;
}

// Drop every hart's translations for asid. Without per-process ASIDs,
// each return to user mode flushes anyway.
void asid_flush(int asid) {
    if (asids && sbi_remote_sfence_vma_asid_all(asid) != SBI_SUCCESS)
        panic("asid_flush: SBI remote sfence failed");
}

// The DMA alias is the linear map (see DMA_OFFSET): nothing to map.
void dma_map(uint64 pa) {
}
