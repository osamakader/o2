#include "mmu.h"
#include "memlayout.h"

// Level 1 block descriptors (1GB each) for the boot page table.
#define BOOT_DEVICE (PTE_ATTRIDX(MT_DEVICE_nGnRE) | PTE_AP_RW | PTE_AF | \
                     PTE_PXN | PTE_UXN | PTE_VALID)
#define BOOT_NORMAL (PTE_ATTRIDX(MT_NORMAL) | PTE_SH_INNER | PTE_AP_RW | \
                     PTE_AF | PTE_VALID)

// Bring-up aid on boards whose firmware has set up the console UART
// (platform.h's EARLY_UART, a PL011): one character straight to it, at
// its physical address, so that how far a CPU got shows before the
// kernel's own console works: A at entry, B in EL1, C with the MMU on.
.macro early_putc ch
#ifdef EARLY_UART
        ldr x9, =EARLY_UART
99:     ldr w10, [x9, #0x18]            // FR: wait while TXFF
        tbnz w10, #5, 99b
        mov w10, #\ch
        str w10, [x9]                   // DR
#endif
.endm

.section .text.boot, "ax"
.global _entry
_entry:
        // An arm64 Linux Image header, so that boot loaders (the Pi
        // firmware, qemu's raspi4b) load the raw image (kernel8.img) at
        // KERNEL_LOAD and start it as they start Linux: in EL2, with the
        // device tree in x0, other CPUs waiting in a spin table.
        b 1f                            // code0
        .long 0                         // code1
        .quad KERNEL_LOAD - KERNBASE    // text_offset, from the start of RAM
        .quad __image_size              // memory the image takes, with BSS
        .quad 0x2                       // flags: little-endian, 4KB pages
        .quad 0, 0, 0
        .ascii "ARM\x64"                // magic
        .long 0
1:
        // mask interrupts
        msr daifset, #0xf

        // qemu and the Raspberry Pi firmware pass the device tree's
        // physical address in x0 (qemu virt: 0, see board_init())
        mov x20, x0
        early_putc 'A'

        // The Raspberry Pi firmware starts the kernel in EL2 (qemu virt
        // with virtualization=on too); o2 runs in EL1: drop there.
        // Entering in EL3 isn't supported.
        mrs x0, CurrentEL
        lsr x0, x0, #2
        cmp x0, #2
        b.ne 5f
        // EL2: EL1 runs AArch64, and may use the counters, timers and
        // FP/SIMD without trapping here
        ldr x0, =HCR_EL2_RW
        msr hcr_el2, x0
        mrs x0, cnthctl_el2
        orr x0, x0, #3                  // EL1PCTEN, EL1PCEN
        msr cnthctl_el2, x0
        msr cntvoff_el2, xzr
        ldr x0, =CPTR_EL2_VALUE
        msr cptr_el2, x0
        msr hstr_el2, xzr
        // a GICv3 CPU interface: let EL1 use its system registers
        mrs x0, id_aa64pfr0_el1
        ubfx x0, x0, #24, #4
        cbz x0, 6f
        mov x0, #0xf                    // SRE, DFB, DIB, Enable
        msr S3_4_C12_C9_5, x0           // ICC_SRE_EL2
        isb
6:      mrs x0, midr_el1
        msr vpidr_el2, x0
        mrs x0, mpidr_el1
        msr vmpidr_el2, x0
        ldr x0, =SCTLR_EL1_RES1         // MMU and caches off
        msr sctlr_el1, x0
        mov x0, #SPSR_EL1H_MASKED
        msr spsr_el2, x0
        adr x0, 5f
        msr elr_el2, x0
        eret
5:      early_putc 'B'
        // this CPU's index (cpuid()): Aff0 + Aff1, one of which is 0 on
        // the machines o2 knows (qemu virt and the Pi 4: Aff0; the Pi 5:
        // Aff1)
        mrs x0, MPIDR_EL1
        and x19, x0, #0xff
        ubfx x1, x0, #8, #8
        add x19, x19, x1
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
        early_putc 'C'                  // the UART is identity-mapped too

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

        // main(boot, dtb) in main.c; CPU 0 is the boot CPU
        cmp x19, #0
        cset w0, eq
        mov x1, x20
        mov x29, xzr            // end of the frame chain (backtraces)
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
// kvminithart() installs the real kernel page table: 1GB blocks for
// the devices (BOOT_DEVICE_GB) and the first BOOT_RAM_GB of RAM, at
// their identity address (low half, needed while the PC is still
// physical) and at P2V() (high half).
#define BOOT_HIGH_IDX ((KVA_OFFSET >> 30) & 0x1ff)
#define RAM_GB0 (KERNBASE >> 30)

.section .data
.balign 4096
boot_pagetable:
        .set i, 0
        .rept 512
        .if i >= BOOT_HIGH_IDX
        .set gb, i - BOOT_HIGH_IDX
        .else
        .set gb, i
        .endif
        .if gb == BOOT_DEVICE_GB
        .quad (gb << 30) | BOOT_DEVICE
        .elseif gb >= RAM_GB0 && gb < RAM_GB0 + BOOT_RAM_GB
        .quad (gb << 30) | BOOT_NORMAL
        .else
        .quad 0
        .endif
        .set i, i + 1
        .endr
