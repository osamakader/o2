// Context switch
//
//   void swtch(struct context *old, struct context *new);
//
// Save current registers in old. Load from new.

.global swtch
.global kthread_trampoline

swtch:
        sd ra, 0(a0)
        sd sp, 8(a0)
        sd s0, 16(a0)
        sd s1, 24(a0)
        sd s2, 32(a0)
        sd s3, 40(a0)
        sd s4, 48(a0)
        sd s5, 56(a0)
        sd s6, 64(a0)
        sd s7, 72(a0)
        sd s8, 80(a0)
        sd s9, 88(a0)
        sd s10, 96(a0)
        sd s11, 104(a0)

        ld ra, 0(a1)
        ld sp, 8(a1)
        ld s0, 16(a1)
        ld s1, 24(a1)
        ld s2, 32(a1)
        ld s3, 40(a1)
        ld s4, 48(a1)
        ld s5, 56(a1)
        ld s6, 64(a1)
        ld s7, 72(a1)
        ld s8, 80(a1)
        ld s9, 88(a1)
        ld s10, 96(a1)
        ld s11, 104(a1)

        ret

// Entry point for a brand-new kernel thread. swtch() lands here via ra
// with s0 = arg and s1 = fn, as context_kthread() set them up.
// (x29/s0 is zeroed so backtraces stop at the thread's first frame.)
// kthread_start() drops the p->lock scheduler() switched in with.
kthread_trampoline:
        mv s2, s0               // arg; s0 is the frame pointer:
        li s0, 0                // end the frame chain (backtraces)
        call kthread_start
        mv a0, s2
        jalr s1
        // fn returned: tear the thread down. kthread_exit() never returns.
        call kthread_exit
