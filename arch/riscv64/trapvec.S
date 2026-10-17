// kernelvec: the trap vector (stvec, direct mode) while the kernel runs.
// Traps from user mode enter through uservec in the trampoline
// (trampoline.S) instead; usertrap() and usertrapret() switch stvec.
//
// Frame layout: struct trapframe (trap.h).

.equ TF_EPC,    32
.equ TF_STATUS, 40
.equ TF_CAUSE,  48
.equ TF_TVAL,   56
.equ TF_SIZE,   320

// Save x1 and x3-x31 into the frame at sp (sp itself, x2, isn't
// needed), then the trap CSRs.
.macro SAVE_REGS
    sd x1, 72(sp)
    sd x3, 88(sp)
    sd x4, 96(sp)
    sd x5, 104(sp)
    sd x6, 112(sp)
    sd x7, 120(sp)
    sd x8, 128(sp)
    sd x9, 136(sp)
    sd x10, 144(sp)
    sd x11, 152(sp)
    sd x12, 160(sp)
    sd x13, 168(sp)
    sd x14, 176(sp)
    sd x15, 184(sp)
    sd x16, 192(sp)
    sd x17, 200(sp)
    sd x18, 208(sp)
    sd x19, 216(sp)
    sd x20, 224(sp)
    sd x21, 232(sp)
    sd x22, 240(sp)
    sd x23, 248(sp)
    sd x24, 256(sp)
    sd x25, 264(sp)
    sd x26, 272(sp)
    sd x27, 280(sp)
    sd x28, 288(sp)
    sd x29, 296(sp)
    sd x30, 304(sp)
    sd x31, 312(sp)
    csrr t0, sepc
    sd t0, TF_EPC(sp)
    csrr t0, sstatus
    sd t0, TF_STATUS(sp)
    csrr t0, scause
    sd t0, TF_CAUSE(sp)
    csrr t0, stval
    sd t0, TF_TVAL(sp)
.endm

// Restore sepc/sstatus and x1, x3, x5-x31 from the frame at sp. Not tp
// (x4): it holds this hart's id, and a thread preempted in kerneltrap()
// may resume on another hart.
.macro RESTORE_REGS
    ld t0, TF_EPC(sp)
    csrw sepc, t0
    ld t0, TF_STATUS(sp)
    csrw sstatus, t0
    ld x1, 72(sp)
    ld x3, 88(sp)
    ld x5, 104(sp)
    ld x6, 112(sp)
    ld x7, 120(sp)
    ld x8, 128(sp)
    ld x9, 136(sp)
    ld x10, 144(sp)
    ld x11, 152(sp)
    ld x12, 160(sp)
    ld x13, 168(sp)
    ld x14, 176(sp)
    ld x15, 184(sp)
    ld x16, 192(sp)
    ld x17, 200(sp)
    ld x18, 208(sp)
    ld x19, 216(sp)
    ld x20, 224(sp)
    ld x21, 232(sp)
    ld x22, 240(sp)
    ld x23, 248(sp)
    ld x24, 256(sp)
    ld x25, 264(sp)
    ld x26, 272(sp)
    ld x27, 280(sp)
    ld x28, 288(sp)
    ld x29, 296(sp)
    ld x30, 304(sp)
    ld x31, 312(sp)
.endm

.section .text
.balign 4
.global kernelvec
kernelvec:
    // Kernel exceptions are fatal; check before touching the stack,
    // which may be the one that just overflowed. t0 is parked in
    // sscratch meanwhile (unused while the kernel runs).
    csrw sscratch, t0
    csrr t0, scause
    bgez t0, kernel_exception   // interrupts have scause bit 63 set
    csrr t0, sscratch

    // Interrupt in the kernel: frame on the current stack.
    addi sp, sp, -TF_SIZE
    SAVE_REGS
    mv a0, sp
    call kerneltrap
    RESTORE_REGS
    addi sp, sp, TF_SIZE
    sret

kernel_exception:
    // Run the fatal handler on this hart's emergency stack:
    // estack + (hartid + 1) * 4096.
    la t0, estack
    addi sp, tp, 1
    slli sp, sp, 12
    add sp, sp, t0
    csrr t0, sscratch
    addi sp, sp, -TF_SIZE
    SAVE_REGS
    mv a0, sp
    call kernel_fatal
1:  wfi
    j 1b
