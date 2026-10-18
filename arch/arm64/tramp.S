#include "memlayout.h"
// The trampoline: the only kernel code mapped while user code runs
// (KPTI). On return to EL0, TTBR1 is switched to tramp_pagetable,
// which maps just this page and tramp_data (mmu.c), at the same
// addresses as in the kernel page table, and VBAR_EL1 points at
// tramp_vectors. An exception from EL0 switches TTBR1 back to the
// kernel page table here, then enters the real handler.
//
// x30 is the scratch register: the entries stash the user's in
// TPIDR_EL1 (EL1-only), and el0_* in exceptions.S restore it.
//
// ASIDs: TTBR0 supplies the ASID. User mode runs with TTBR0 = the
// process's table and ASID; the kernel always with the empty table and
// ASID 0. All mappings are non-global, so the TLB keeps kernel and
// user translations apart and no flush is needed on either crossing.
// Without ARM64_ASID (the default) user mode runs under ASID 0 too, and
// tramp_exit flushes the TLB instead.

.section trampsec, "ax"
.balign 4096

// Enter the kernel from EL0: switch to the kernel page table and
// branch to handler, with the user's x30 in TPIDR_EL1.
.macro TRAMP_ENTRY handler
    .balign 128
    msr tpidr_el1, x30
    ldr x30, 1f
    ldr x30, [x30, #16]     // tramp_data[2]: empty TTBR0, ASID 0
    msr ttbr0_el1, x30      // first leave the user ASID ...
    isb
    ldr x30, 1f
    ldr x30, [x30]          // tramp_data[0]: kernel TTBR1
    msr ttbr1_el1, x30      // ... then map the kernel, under ASID 0
    isb
    // Both TTBR1 tables map the kernel's region, differently, under
    // ASID 0: the TLB's walk cache may still hold the trampoline
    // table's intermediate entries for it (qemu has none, a Cortex-A72
    // does), which lead nowhere in the kernel. Drop ASID 0's.
    tlbi aside1, xzr
    dsb nsh
    isb
    ldr x30, 2f
    br x30
    .balign 8
1:  .quad tramp_data
2:  .quad \handler
.endm

// Exceptions from EL1 can't happen while the trampoline is installed
// (interrupts are masked from the switch to eret); hang if one does.
.macro TRAMP_HANG
    .balign 128
#ifdef EARLY_UART
    b tramp_hang
#endif
1:  b 1b
.endm

.global tramp_vectors
tramp_vectors:
    /* Current EL with SP0 */
    TRAMP_HANG
    TRAMP_HANG
    TRAMP_HANG
    TRAMP_HANG
    /* Current EL with SPn */
    TRAMP_HANG
    TRAMP_HANG
    TRAMP_HANG
    TRAMP_HANG
    /* Lower EL with Aarch64 */
    TRAMP_ENTRY el0_sync
    TRAMP_ENTRY el0_irq
    TRAMP_ENTRY el1_fiq
    TRAMP_ENTRY el1_serr
    /* Lower EL with Aarch32: not supported, fatal */
    TRAMP_ENTRY el1_sync
    TRAMP_ENTRY el1_sync
    TRAMP_ENTRY el1_fiq
    TRAMP_ENTRY el1_serr

// Last step of ret_to_user: user registers are restored except x30,
// which is in TPIDR_EL1, VBAR_EL1 = tramp_vectors, and uvmswitch()
// staged the user TTBR0 (table and ASID) in TPIDRRO_EL0. Unmap the
// kernel first, then enter the user ASID, and return to EL0.
.balign 128
.global tramp_exit
tramp_exit:
    ldr x30, 1f
    ldr x30, [x30, #8]      // tramp_data[1]: trampoline TTBR1
    msr ttbr1_el1, x30
    isb
    mrs x30, tpidrro_el0
    msr ttbr0_el1, x30
    msr tpidrro_el0, xzr    // EL0 can read it: don't leave the PA there
    isb
#ifndef ARM64_ASID
    // one ASID for everything: no kernel translation may survive
    tlbi vmalle1
    dsb nsh
    isb
#endif
    mrs x30, tpidr_el1
    eret
.balign 8
1:  .quad tramp_data

#ifdef EARLY_UART
// Bring-up aid: an exception in EL1 with the trampoline installed
// (which otherwise just hangs, unseen): print !, then ESR, ELR, FAR and
// SPSR to the UART, which tramp_pagetable maps too, and hang.
tramp_hang:
    ldr x4, =(KVA_OFFSET + EARLY_UART)
    mov x1, #'!'
    bl uputc
    mrs x2, esr_el1
    bl uhex
    mrs x2, elr_el1
    bl uhex
    mrs x2, far_el1
    bl uhex
    mrs x2, spsr_el1
    bl uhex
1:  b 1b
uputc:
    ldr w3, [x4, #0x18]
    tbnz w3, #5, uputc
    str w1, [x4]
    ret
uhex:
    mov x6, x30
    mov x1, #' '
    bl uputc
    mov x5, #60
2:  lsr x1, x2, x5
    and x1, x1, #0xf
    add x7, x1, #'0'
    add x8, x1, #('a' - 10)
    cmp x1, #10
    csel x1, x7, x8, lt
    bl uputc
    subs x5, x5, #4
    b.ge 2b
    mov x30, x6
    ret
.ltorg
#endif
