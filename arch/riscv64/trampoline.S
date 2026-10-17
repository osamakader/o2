// The trampoline: the only kernel code mapped in user page tables, at
// TRAMPOLINE in both user and kernel page tables (from xv6). While a
// process runs in user mode, stvec points at uservec here; uservec
// saves the user registers in the process's trap frame page (mapped at
// TRAPFRAME) and switches to the kernel page table, and userret does
// the reverse. The section is one page, placed by kernel.ld.

#include "mmu.h"

.section trampsec, "ax"
.balign 4096
.global trampoline
trampoline:

.balign 4
.global uservec
uservec:
        // trap from user mode: still on the user page table, which maps
        // only the user's memory, this page and TRAPFRAME.

        // free a0 to address the trap frame
        csrw sscratch, a0
        li a0, TRAPFRAME

        // save the user registers in TRAPFRAME
        sd ra, 72(a0)
        sd sp, 80(a0)
        sd gp, 88(a0)
        sd tp, 96(a0)
        sd t0, 104(a0)
        sd t1, 112(a0)
        sd t2, 120(a0)
        sd s0, 128(a0)
        sd s1, 136(a0)
        sd a1, 152(a0)
        sd a2, 160(a0)
        sd a3, 168(a0)
        sd a4, 176(a0)
        sd a5, 184(a0)
        sd a6, 192(a0)
        sd a7, 200(a0)
        sd s2, 208(a0)
        sd s3, 216(a0)
        sd s4, 224(a0)
        sd s5, 232(a0)
        sd s6, 240(a0)
        sd s7, 248(a0)
        sd s8, 256(a0)
        sd s9, 264(a0)
        sd s10, 272(a0)
        sd s11, 280(a0)
        sd t3, 288(a0)
        sd t4, 296(a0)
        sd t5, 304(a0)
        sd t6, 312(a0)

        // the user's a0
        csrr t0, sscratch
        sd t0, 144(a0)

        // kernel stack, hart id and usertrap() from the trap frame
        ld sp, 8(a0)
        ld tp, 24(a0)
        ld t0, 16(a0)

        // switch to the kernel page table. Its translations are tagged
        // with the kernel's ASID, so no fence is needed: the user's and
        // the kernel's live side by side in the TLB.
        ld t1, 0(a0)
        csrw satp, t1

        // usertrap() doesn't return
        jr t0

// tramp_userret(satp, flush): called by usertrapret() at its
// TRAMPOLINE alias to switch to the user page table and return to user
// mode. With per-process ASIDs, the kernel's translations stay in the
// TLB under ASID 0, out of the user's reach; without, flush is set and
// they're dropped here.
.global tramp_userret
tramp_userret:
        csrw satp, a0
        beqz a1, 1f
        sfence.vma zero, zero
1:

        li a0, TRAPFRAME

        // restore all but a0 from TRAPFRAME
        ld ra, 72(a0)
        ld sp, 80(a0)
        ld gp, 88(a0)
        ld tp, 96(a0)
        ld t0, 104(a0)
        ld t1, 112(a0)
        ld t2, 120(a0)
        ld s0, 128(a0)
        ld s1, 136(a0)
        ld a1, 152(a0)
        ld a2, 160(a0)
        ld a3, 168(a0)
        ld a4, 176(a0)
        ld a5, 184(a0)
        ld a6, 192(a0)
        ld a7, 200(a0)
        ld s2, 208(a0)
        ld s3, 216(a0)
        ld s4, 224(a0)
        ld s5, 232(a0)
        ld s6, 240(a0)
        ld s7, 248(a0)
        ld s8, 256(a0)
        ld s9, 264(a0)
        ld s10, 272(a0)
        ld s11, 280(a0)
        ld t3, 288(a0)
        ld t4, 296(a0)
        ld t5, 304(a0)
        ld t6, 312(a0)

        // restore user a0
        ld a0, 144(a0)

        // return to user mode and user pc; usertrapret() set up sstatus
        // and sepc.
        sret
