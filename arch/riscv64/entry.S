#include "mmu.h"
#include "memlayout.h"

// OpenSBI enters S-mode here with the MMU off: a0 = hartid, a1 = DTB.
// The boot hart arrives at _entry; the others at _entry_secondary,
// started by start_cpus() through SBI HSM.

// Level 2 (1GB) leaves for the boot page table.
#define BOOT_DEVICE (PTE_R | PTE_W | PTE_A | PTE_D | PTE_VALID)
#define BOOT_NORMAL (PTE_R | PTE_W | PTE_X | PTE_A | PTE_D | PTE_VALID)
#define GIGA_PTE(pa, flags) ((((pa) >> 12) << 10) | (flags))

.section .text.boot, "ax"
.global _entry
.global _entry_secondary
_entry:
        li s1, 1                // boot hart
        j 1f
_entry_secondary:
        li s1, 0
1:
        csrw sie, zero
        csrci sstatus, 2        // SIE off
        mv tp, a0               // tp = hartid while in the kernel
        mv s2, a1               // the device tree's physical address

        beqz s1, 3f

        // Clear BSS on the boot hart. The MMU is off; with -mcmodel=medany
        // la is PC-relative, so it yields physical addresses here.
        la t0, __bss_start
        la t1, __bss_end
2:      bgeu t0, t1, 3f
        sd zero, 0(t0)
        addi t0, t0, 8
        j 2b

3:      // Turn on Sv39 with boot_pagetable.
        la t0, boot_pagetable
        srli t0, t0, 12
        li t1, SATP_SV39
        or t0, t0, t1
        sfence.vma zero, zero
        csrw satp, t0
        sfence.vma zero, zero

        // Jump from the physical (identity-mapped) address to the
        // high-half link address of the code below.
        lla t0, 4f
        ld t0, 0(t0)
        jr t0
.balign 8
4:      .dword 5f
5:
        // set up a stack for C: stack0 + (hartid + 1) * 4096
        la sp, stack0
        addi t0, tp, 1
        slli t0, t0, 12
        add sp, sp, t0

        // kernel traps go to kernelvec (trapvec.S)
        la t0, kernelvec
        csrw stvec, t0

        // main(boot, dtb) in main.c
        mv a0, s1
        mv a1, s2
        li s0, 0                // end of the frame chain (backtraces)
        call main
6:      wfi
        j 6b

// Coarse boot page table: the first 1GB (devices) and the 1GB of RAM
// at 0x80000000, both at their identity address (needed while the PC
// is still physical) and at P2V(). kvminithart() replaces it.
#define BOOT_HIGH_IDX ((KVA_OFFSET >> 30) & 0x1ff)

.section .data
.balign 4096
boot_pagetable:
        .dword GIGA_PTE(0x00000000, BOOT_DEVICE)   // [0, 1GB): devices
        .dword 0
        .dword GIGA_PTE(KERNBASE, BOOT_NORMAL)     // [2GB, 3GB): RAM
        .fill BOOT_HIGH_IDX - 3, 8, 0
        .dword GIGA_PTE(0x00000000, BOOT_DEVICE)   // P2V(devices)
        .dword 0
        .dword GIGA_PTE(KERNBASE, BOOT_NORMAL)     // P2V(RAM)
        .fill 512 - BOOT_HIGH_IDX - 3, 8, 0
