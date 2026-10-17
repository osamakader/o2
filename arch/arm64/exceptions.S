.section .vectors, "ax"

.equ TF_X0,   0
.equ TF_X30,  240
.equ TF_ELR,  248
.equ TF_SPSR, 256
.equ TF_ESR,  264
.equ TF_FAR,  272
.equ TF_SP,   280   // user SP_EL0, only valid for traps from EL0
.equ TF_SIZE, 288

.macro SAVE_TRAPFRAME
    # save registers
    sub sp, sp, #TF_SIZE
    
	stp x0, x1, [sp, #TF_X0]
	stp x2, x3, [sp, #TF_X0 + 16]
	stp x4, x5, [sp, #TF_X0 + 32]
	stp x6, x7, [sp, #TF_X0 + 48]
	stp x8, x9, [sp, #TF_X0 + 64]
	stp x10, x11, [sp, #TF_X0 + 80]
	stp x12, x13, [sp, #TF_X0 + 96]
	stp x14, x15, [sp, #TF_X0 + 112]
	stp x16, x17, [sp, #TF_X0 + 128]
	stp x18, x19, [sp, #TF_X0 + 144]
	stp x20, x21, [sp, #TF_X0 + 160]
	stp x22, x23, [sp, #TF_X0 + 176]
	stp x24, x25, [sp, #TF_X0 + 192]
	stp x26, x27, [sp, #TF_X0 + 208]
	stp x28, x29, [sp, #TF_X0 + 224]
	str x30, [sp, #TF_X30]

	mrs x2, ELR_EL1
	mrs x3, SPSR_EL1
	mrs x1, ESR_EL1
	mrs x4, FAR_EL1
	str x2, [sp, #TF_ELR]
	str x3, [sp, #TF_SPSR]
	str x1, [sp, #TF_ESR]
	str x4, [sp, #TF_FAR]
    mov x0, sp
.endm

.macro RESTORE_TRAPFRAME
	ldr x2, [sp, #TF_ELR]
	ldr x3, [sp, #TF_SPSR]
	msr ELR_EL1, x2
	msr SPSR_EL1, x3

	ldp x0, x1, [sp, #TF_X0]
	ldp x2, x3, [sp, #TF_X0 + 16]
	ldp x4, x5, [sp, #TF_X0 + 32]
	ldp x6, x7, [sp, #TF_X0 + 48]
	ldp x8, x9, [sp, #TF_X0 + 64]
	ldp x10, x11, [sp, #TF_X0 + 80]
	ldp x12, x13, [sp, #TF_X0 + 96]
	ldp x14, x15, [sp, #TF_X0 + 112]
	ldp x16, x17, [sp, #TF_X0 + 128]
	ldp x18, x19, [sp, #TF_X0 + 144]
	ldp x20, x21, [sp, #TF_X0 + 160]
	ldp x22, x23, [sp, #TF_X0 + 176]
	ldp x24, x25, [sp, #TF_X0 + 192]
	ldp x26, x27, [sp, #TF_X0 + 208]
	ldp x28, x29, [sp, #TF_X0 + 224]
	ldr x30, [sp, #TF_X30]
	add sp, sp, #TF_SIZE
.endm

.balign 2048
.global _vectors
_vectors:
    /* Current EL with SP0 */
    b el1_sync  /* Synchronous */
    .balign 128
    b irq_addr  /* IRQ/vIRQ */
    .balign 128
    b el1_fiq   /* FIQ/vFIQ */
    .balign 128
    b el1_serr  /* SError/vSError */
    .balign 128
    /* Current EL with SPn */
    b el1_sync  /* Synchronous */
    .balign 128
    b irq_addr  /* IRQ/vIRQ */
    .balign 128
    b el1_fiq   /* FIQ/vFIQ */
    .balign 128
    b el1_serr  /* SError/vSError */
    .balign 128
    /* Lower EL with Aarch64: EL0 runs with VBAR_EL1 = tramp_vectors
     * (tramp.S), so these can only be taken by mistake: fatal. */
    b el1_sync  /* Synchronous */
    .balign 128
    b el1_sync  /* IRQ/vIRQ */
    .balign 128
    b el1_fiq   /* FIQ/vFIQ */
    .balign 128
    b el1_serr  /* SError/vSError */
    .balign 128
    /* Lower EL with Aarch32: not supported, fatal */
    b el1_sync  /* Synchronous */
    .balign 128
    b el1_sync  /* IRQ/vIRQ */
    .balign 128
    b el1_fiq   /* FIQ/vFIQ */
    .balign 128
    b el1_serr  /* SError/vSError */
    .balign 128

.section .text, "ax"

// Synchronous exceptions, FIQs and SErrors taken from EL1 are fatal.
// Handle them on this CPU's emergency stack (SP_EL0, set in entry.S)
// rather than the current kernel stack: if that stack just overflowed
// into its guard page, pushing a trapframe there would fault again and
// walk down into the next kernel stack.
.macro FATAL_ENTRY handler
    msr spsel, #0
    SAVE_TRAPFRAME
    bl \handler
1:  wfi
    b 1b
.endm

.global el1_sync, el1_fiq, el1_serr
el1_sync:
    FATAL_ENTRY sync_handler
el1_fiq:
    FATAL_ENTRY fiq_handler
el1_serr:
    FATAL_ENTRY serr_handler

// IRQ taken from EL1, on the current kernel stack.
irq_addr:
    SAVE_TRAPFRAME
    bl handle_irq
    RESTORE_TRAPFRAME
    eret

// Exceptions from EL0 come through tramp_vectors (tramp.S), which has
// switched TTBR1 back to the kernel page table and left the user's x30
// in TPIDR_EL1. They arrive on SP_EL1, which is the top of the running
// process's kernel stack (the kernel stack is empty while user code
// runs), so the trapframe lands at a fixed place on it. SP_EL0 holds
// the user stack pointer: save it in the trapframe and point SP_EL0
// back at this CPU's emergency stack while in the kernel. Exceptions
// from here on go to the kernel's vectors.
.macro EL0_ENTRY
    mrs x30, tpidr_el1
    SAVE_TRAPFRAME
    ldr x1, =_vectors
    msr vbar_el1, x1
    isb
    mrs x1, sp_el0
    str x1, [sp, #TF_SP]
    mrs x1, mpidr_el1
    and x1, x1, #0xff
    add x1, x1, #1
    lsl x1, x1, #12
    ldr x2, =estack
    add x1, x1, x2
    msr sp_el0, x1
    mov x0, sp
.endm

.global el0_sync, el0_irq
el0_sync:
    EL0_ENTRY
    bl usertrap
    b ret_to_user

el0_irq:
    EL0_ENTRY
    bl usertrap_irq
    b ret_to_user

// userret(tf): enter user mode for the first time (forkret), from the
// trapframe at the top of the kernel stack.
.global userret
userret:
    mov sp, x0
    b ret_to_user

// Return to EL0 with sp pointing at the process's trapframe.
// usertrapret() leaves interrupts off and installs the user page table
// in TTBR0; tramp_exit (tramp.S) then hides the kernel by switching
// TTBR1 to the trampoline page table, and erets, which restores the
// user's PSTATE (interrupts on) from SPSR. Exceptions from EL0 go to
// tramp_vectors from now on.
.global ret_to_user
ret_to_user:
    mov x0, sp
    bl usertrapret
    ldr x1, [sp, #TF_SP]
    msr sp_el0, x1
    RESTORE_TRAPFRAME
    msr tpidr_el1, x30
    ldr x30, =tramp_vectors
    msr vbar_el1, x30
    isb
    ldr x30, =tramp_exit
    br x30
