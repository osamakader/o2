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
    b irq_handler
fiq_addr:
    b fiq_handler
serr_addr:
    b serr_handler
