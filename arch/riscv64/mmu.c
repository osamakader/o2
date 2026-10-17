#include "types.h"
#include "memlayout.h"
#include "vm.h"
#include "mmu.h"
#include "cpu.h"

// Map the qemu virt devices the kernel drives: UART, PLIC.
void kvmmap_devices(pagetable_t pagetable) {
    kvmmap(pagetable, P2V(UART0), UART0, PGSIZE, PTE_KERN_DEVICE);
    kvmmap(pagetable, P2V(PLIC), PLIC, PLIC_SIZE, PTE_KERN_DEVICE);
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

// A new user root table shares the kernel's upper-half entries: Sv39
// has a single root (satp), so the kernel stays mapped in every user
// address space. All kernel mappings exist before the first process.
void uvminit_root(uint64 *root) {
    for (int i = UVM_ROOT_ENTRIES; i < 512; i++)
        root[i] = kernel_pagetable[i];
}

// Make code the kernel wrote visible to instruction fetch. fence.i
// only covers this hart, so uvmswitch() repeats it on every return to
// user mode, wherever the process runs.
void sync_icache(uint64 va, uint64 len) {
    asm volatile("fence.i" ::: "memory");
}

// Install a user page table on this hart. Its upper half is the
// kernel's, so the kernel keeps running on it until kvmswitch(). Drop
// stale translations, and make code loaded by exec() on any hart
// visible to this hart's instruction fetch.
void uvmswitch(pagetable_t pagetable) {
    asm volatile("sfence.vma zero, zero");
    csr_write(satp, MAKE_SATP(V2P(pagetable)));
    asm volatile("sfence.vma zero, zero; fence.i" ::: "memory");
}

// Leave a user address space: back to kernel_pagetable, so a page
// table about to be freed isn't in use.
void kvmswitch(void) {
    csr_write(satp, MAKE_SATP(V2P(kernel_pagetable)));
    asm volatile("sfence.vma zero, zero");
}
