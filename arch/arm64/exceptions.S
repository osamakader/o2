.section .vectors, "ax"

.equ TF_X0,   0
.equ TF_X30,  240
.equ TF_ELR,  248
.equ TF_SPSR, 256
.equ TF_ESR,  264
.equ TF_FAR,  272
.equ TF_SIZE, 288

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
    bl handle_irq
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
	eret
fiq_addr:
    b fiq_handler
serr_addr:
    b serr_handler
