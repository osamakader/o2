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
