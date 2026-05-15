.section .vectors, "ax"
.balign 2048
.global _vectors
_vectors:
    /* Current EL with SP0 */
    b sync_addr /* Synchronous */
    .balign 128
    b irq_addr  /* IRQ/vIRQ */
    .balign 128
    b fiq_addr  /* FIQ/vFIQ */
    .balign 128
    b serr_addr /* SError/vSError */
    .balign 128
    /* Current EL with SPn */
    b sync_addr /* Synchronous */
    .balign 128
    b irq_addr  /* IRQ/vIRQ */
    .balign 128
    b fiq_addr  /* FIQ/vFIQ */
    .balign 128
    b serr_addr /* SError/vSError */
    .balign 128
    /* Lower EL with Aarch64 */
    b sync_addr /* Synchronous */
    .balign 128
    b irq_addr  /* IRQ/vIRQ */
    .balign 128
    b fiq_addr  /* FIQ/vFIQ */
    .balign 128
    b serr_addr /* SError/vSError */
    .balign 128
    /* Lower EL with Aarch32 */
    b sync_addr /* Synchronous */
    .balign 128
    b irq_addr  /* IRQ/vIRQ */
    .balign 128
    b fiq_addr  /* FIQ/vFIQ */
    .balign 128
    b serr_addr /* SError/vSError */
    .balign 128

.section .text, "ax"

sync_addr:
    b sync_handler
irq_addr:
    # save registers
    sub sp, sp, #288
    
	stp x0, x1, [sp, #0]
	stp x2, x3, [sp, #16]
	stp x4, x5, [sp, #32]
	stp x6, x7, [sp, #48]
	stp x8, x9, [sp, #64]
	stp x10, x11, [sp, #80]
	stp x12, x13, [sp, #96]
	stp x14, x15, [sp, #112]
	stp x16, x17, [sp, #128]
	stp x18, x19, [sp, #144]
	stp x20, x21, [sp, #160]
	stp x22, x23, [sp, #176]
	stp x24, x25, [sp, #192]
	stp x26, x27, [sp, #208]
	stp x28, x29, [sp, #224]
	str x30, [sp, #240]

	mrs x2, ELR_EL1
	mrs x3, SPSR_EL1
	mrs x1, ESR_EL1
	mrs x4, FAR_EL1
	str x2, [sp, #248]
	str x3, [sp, #256]
	str x1, [sp, #264]
	str x4, [sp, #272]
    mov x0, sp
    bl handle_irq
	ldr x2, [sp, #248]
	ldr x3, [sp, #256]
	msr ELR_EL1, x2
	msr SPSR_EL1, x3

	ldp x0, x1, [sp, #0]
	ldp x2, x3, [sp, #16]
	ldp x4, x5, [sp, #32]
	ldp x6, x7, [sp, #48]
	ldp x8, x9, [sp, #64]
	ldp x10, x11, [sp, #80]
	ldp x12, x13, [sp, #96]
	ldp x14, x15, [sp, #112]
	ldp x16, x17, [sp, #128]
	ldp x18, x19, [sp, #144]
	ldp x20, x21, [sp, #160]
	ldp x22, x23, [sp, #176]
	ldp x24, x25, [sp, #192]
	ldp x26, x27, [sp, #208]
	ldp x28, x29, [sp, #224]
	ldr x30, [sp, #240]
	add sp, sp, #288
	eret
fiq_addr:
    b fiq_handler
serr_addr:
    b serr_handler
