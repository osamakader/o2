// The single trap vector (stvec, direct mode) for traps from user mode
// and from the kernel.
//
// sscratch tells them apart: while a process runs in user mode it
// holds the address of that process's trapframe (at the top of its
// kernel stack); in the kernel it is 0.

.equ TF_EPC,    256
.equ TF_STATUS, 264
.equ TF_CAUSE,  272
.equ TF_TVAL,   280
.equ TF_HARTID, 288
.equ TF_SIZE,   304

// Save x1 and x3-x31 into the frame at sp (sp itself, x2, is handled
// by the caller), then the trap CSRs.
.macro SAVE_REGS
    sd x1, 8(sp)
    sd x3, 24(sp)
    sd x4, 32(sp)
    sd x5, 40(sp)
    sd x6, 48(sp)
    sd x7, 56(sp)
    sd x8, 64(sp)
    sd x9, 72(sp)
    sd x10, 80(sp)
    sd x11, 88(sp)
    sd x12, 96(sp)
    sd x13, 104(sp)
    sd x14, 112(sp)
    sd x15, 120(sp)
    sd x16, 128(sp)
    sd x17, 136(sp)
    sd x18, 144(sp)
    sd x19, 152(sp)
    sd x20, 160(sp)
    sd x21, 168(sp)
    sd x22, 176(sp)
    sd x23, 184(sp)
    sd x24, 192(sp)
    sd x25, 200(sp)
    sd x26, 208(sp)
    sd x27, 216(sp)
    sd x28, 224(sp)
    sd x29, 232(sp)
    sd x30, 240(sp)
    sd x31, 248(sp)
    csrr t0, sepc
    sd t0, TF_EPC(sp)
    csrr t0, sstatus
    sd t0, TF_STATUS(sp)
    csrr t0, scause
    sd t0, TF_CAUSE(sp)
    csrr t0, stval
    sd t0, TF_TVAL(sp)
.endm

// Restore sepc/sstatus and x1, x3-x31 from the frame at sp.
.macro RESTORE_REGS
    ld t0, TF_EPC(sp)
    csrw sepc, t0
    ld t0, TF_STATUS(sp)
    csrw sstatus, t0
    ld x1, 8(sp)
    ld x3, 24(sp)
    ld x4, 32(sp)
    ld x5, 40(sp)
    ld x6, 48(sp)
    ld x7, 56(sp)
    ld x8, 64(sp)
    ld x9, 72(sp)
    ld x10, 80(sp)
    ld x11, 88(sp)
    ld x12, 96(sp)
    ld x13, 104(sp)
    ld x14, 112(sp)
    ld x15, 120(sp)
    ld x16, 128(sp)
    ld x17, 136(sp)
    ld x18, 144(sp)
    ld x19, 152(sp)
    ld x20, 160(sp)
    ld x21, 168(sp)
    ld x22, 176(sp)
    ld x23, 184(sp)
    ld x24, 192(sp)
    ld x25, 200(sp)
    ld x26, 208(sp)
    ld x27, 216(sp)
    ld x28, 224(sp)
    ld x29, 232(sp)
    ld x30, 240(sp)
    ld x31, 248(sp)
.endm

.section .text
.balign 4
.global trapvec
trapvec:
    csrrw sp, sscratch, sp      // swap sp and sscratch
    bnez sp, from_user          // sscratch was non-zero: user mode
    csrrw sp, sscratch, sp      // from the kernel: undo, sscratch = 0

    // Kernel exceptions are fatal; check before touching the stack,
    // which may be the one that just overflowed. t0 is parked in
    // sscratch meanwhile.
    csrw sscratch, t0
    csrr t0, scause
    bgez t0, kernel_exception   // interrupts have scause bit 63 set
    csrr t0, sscratch
    csrw sscratch, zero

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
    csrw sscratch, zero
    addi sp, sp, -TF_SIZE
    SAVE_REGS
    mv a0, sp
    call kernel_fatal
1:  wfi
    j 1b

from_user:
    // sp = this process's trapframe, sscratch = user sp.
    SAVE_REGS
    csrr t0, sscratch
    sd t0, 16(sp)               // user sp
    csrw sscratch, zero         // in the kernel now
    ld tp, TF_HARTID(sp)        // kernel tp: this hart's id
    mv a0, sp
    call usertrap
    j ret_to_user

// userret(tf): enter user mode for the first time (forkret), from the
// trapframe at the top of the kernel stack.
.global userret
userret:
    mv sp, a0
    // fall through

// Return to user mode with sp pointing at the process's trapframe.
// usertrapret() leaves interrupts off and installs the user page
// table; sret restores the user's SIE from SPIE.
.global ret_to_user
ret_to_user:
    mv a0, sp
    call usertrapret
    RESTORE_REGS
    csrw sscratch, sp           // where the next trap from user mode saves
    ld sp, 16(sp)               // user sp, last
    sret
