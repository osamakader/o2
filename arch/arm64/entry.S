#include "mmu.h"
#include "memlayout.h"

// Level 1 block descriptors (1GB each) for the boot page table.
#define BOOT_DEVICE (PTE_ATTRIDX(MT_DEVICE_nGnRE) | PTE_AP_RW | PTE_AF | \
                     PTE_PXN | PTE_UXN | PTE_VALID)
#define BOOT_NORMAL (PTE_ATTRIDX(MT_NORMAL) | PTE_SH_INNER | PTE_AP_RW | \
                     PTE_AF | PTE_VALID)

.section .text.boot, "ax"
.global _entry
_entry:
        // mask interrupts
        msr daifset, #0xf

        mrs x19, MPIDR_EL1
        and x19, x19, #0xff
        cbnz x19, 2f

        // Clear BSS on CPU 0. The MMU is off, so only PC-relative
        // addressing (adrp) is used until it is on.
        adrp x0, __bss_start
        add x0, x0, :lo12:__bss_start
        adrp x1, __bss_end
        add x1, x1, :lo12:__bss_end
1:      cmp x0, x1
        b.hs 2f
        str xzr, [x0], #8
        b 1b

2:      bl mmu_on

        // Jump from the physical (identity-mapped) address to the
        // high-half link address of the code below.
        ldr x0, =3f
        br x0
3:
        // set up a stack for C: stack0 + (cpu id + 1) * 4096
        ldr x0, =stack0
        add x1, x19, #1
        lsl x1, x1, #12
        add sp, x0, x1

        // SP_EL0 is unused at EL1: point it at this CPU's emergency
        // stack for fatal exceptions (exceptions.S).
        ldr x0, =estack
        add x0, x0, x1
        msr sp_el0, x0

        // set up the vector table
        ldr x0, =_vectors
        msr vbar_el1, x0
        isb

        // main(boot) in main.c; CPU 0 is the boot CPU
        cmp x19, #0
        cset w0, eq
        bl main
        b .

// Turn on the MMU with boot_pagetable. Leaf, no stack; clobbers x0-x2.
mmu_on:
        ldr x0, =MAIR_VALUE
        msr mair_el1, x0

        // TCR, with IPS set to this core's physical address size
        // (capped at 48 bits)
        ldr x0, =TCR_VALUE
        mrs x1, id_aa64mmfr0_el1
        ubfx x1, x1, #0, #4
        mov x2, #5
        cmp x1, x2
        csel x1, x1, x2, ls
        bfi x0, x1, #TCR_IPS_SHIFT, #3
        msr tcr_el1, x0

        adrp x0, boot_pagetable
        msr ttbr0_el1, x0
        msr ttbr1_el1, x0
        isb
        tlbi vmalle1
        dsb nsh
        isb

        mrs x0, sctlr_el1
        ldr x1, =(SCTLR_M | SCTLR_C | SCTLR_I)
        orr x0, x0, x1
        msr sctlr_el1, x0
        isb
        ret

// Coarse boot page table, loaded into both TTBR0 and TTBR1 until
// kvminithart() installs the real kernel page table. The same first
// 2GB of physical memory appear at the identity address (low half,
// needed while the PC is still physical) and at P2V() (high half).
#define BOOT_HIGH_IDX ((KVA_OFFSET >> 30) & 0x1ff)

.section .data
.balign 4096
boot_pagetable:
        .quad 0x00000000 | BOOT_DEVICE  // [0, 1GB): UART, GIC
        .quad KERNBASE | BOOT_NORMAL    // [1GB, 2GB): RAM, kernel
        .fill BOOT_HIGH_IDX - 2, 8, 0
        .quad 0x00000000 | BOOT_DEVICE  // P2V([0, 1GB))
        .quad KERNBASE | BOOT_NORMAL    // P2V([1GB, 2GB))
        .fill 512 - BOOT_HIGH_IDX - 2, 8, 0
