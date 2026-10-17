#include "types.h"
#include "memlayout.h"
#include "vm.h"
#include "mmu.h"
#include "proc.h"
#include "cpu.h"
#include "defs.h"
#include "console.h"

extern char trampoline[];  // trampoline.S

// Map the qemu virt devices the kernel drives: UART, PLIC, virtio.
void kvmmap_devices(pagetable_t pagetable) {
    kvmmap(pagetable, P2V(VIRTIO_MMIO), VIRTIO_MMIO,
           VIRTIO_MMIO_COUNT * VIRTIO_MMIO_STRIDE, PTE_KERN_DEVICE);
    kvmmap(pagetable, P2V(UART0), UART0, PGSIZE, PTE_KERN_DEVICE);
    kvmmap(pagetable, P2V(PLIC), PLIC, PLIC_SIZE, PTE_KERN_DEVICE);
    kvmmap(pagetable, TRAMPOLINE, V2P(trampoline), PGSIZE, PTE_R | PTE_X);
}

// Switch this hart from the boot page table (entry.S) to
// kernel_pagetable.
void kvminithart(void) {
    // wait for any previous writes to the page table memory to finish.
    asm volatile("sfence.vma zero, zero");

    csr_write(satp, MAKE_SATP(V2P(kernel_pagetable)));

    // flush stale entries from the TLB.
    asm volatile("sfence.vma zero, zero");
}

// Make code the kernel wrote visible to instruction fetch. fence.i
// only covers this hart, so usertrapret() repeats it on every return to
// user mode, wherever the process runs.
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

// Every return to user mode flushes this hart's TLB, so there are no
// tagged translations to drop yet.
void asid_flush(int asid) {
}
