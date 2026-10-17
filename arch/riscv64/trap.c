// Traps: interrupts and exceptions from the kernel and from user mode.

#include "types.h"
#include "param.h"
#include "memlayout.h"
#include "console.h"
#include "proc.h"
#include "cpu.h"
#include "trap.h"
#include "timer.h"
#include "intc.h"
#include "vm.h"
#include "defs.h"
#include "mmu.h"

#define SCAUSE_INTR (1UL << 63)

static const char *cause_name(uint64 cause)
{
    switch (cause) {
    case 0:  return "instruction address misaligned";
    case 1:  return "instruction access fault";
    case 2:  return "illegal instruction";
    case 3:  return "breakpoint";
    case 4:  return "load address misaligned";
    case 5:  return "load access fault";
    case 6:  return "store address misaligned";
    case 7:  return "store access fault";
    case 8:  return "ecall from U-mode";
    case 9:  return "ecall from S-mode";
    case 12: return "instruction page fault";
    case 13: return "load page fault";
    case 15: return "store page fault";
    default: return "other";
    }
}

// Exception in the kernel: fatal. trapvec.S runs this on the hart's
// emergency stack.
void kernel_fatal(struct trapframe *tf)
{
    panic("kernel trap: %s (scause 0x%lx)\n  sepc %p stval %p",
          cause_name(tf->cause), tf->cause, (void *)tf->epc, (void *)tf->tval);
}

void timer_rearm(void);
int plic_claim(void);
void plic_complete(int irq);

#define DEV_NONE  0
#define DEV_TIMER 1
#define DEV_OTHER 2

// Handle a device interrupt; returns which kind it was.
static int devintr(uint64 cause)
{
    if (cause == (SCAUSE_INTR | 5)) {
        // supervisor timer interrupt
        clockintr();
        timer_rearm();
        return DEV_TIMER;
    }

    if (cause == (SCAUSE_INTR | 9)) {
        // supervisor external interrupt, via the PLIC
        int irq = plic_claim();

        if (irq && !irq_handle(irq))
            printf("unexpected interrupt irq=%d\n", irq);

        // the PLIC allows each device to raise at most one interrupt
        // at a time; tell the PLIC the device is now allowed to
        // interrupt again.
        if (irq)
            plic_complete(irq);
        return DEV_OTHER;
    }

    return DEV_NONE;
}

// Interrupt in the kernel, on the current kernel stack. sepc and
// sstatus are saved in tf, so yielding here is safe.
void kerneltrap(struct trapframe *tf)
{
    if ((tf->status & SSTATUS_SPP) == 0)
        panic("kerneltrap: not from supervisor mode");
    if (intr_get())
        panic("kerneltrap: interrupts enabled");

    int dev = devintr(tf->cause);
    if (dev == DEV_NONE)
        panic("kerneltrap: unexpected scause 0x%lx", tf->cause);

    // Preempt the running thread on each tick.
    struct proc *p = myproc();
    if (dev == DEV_TIMER && p && p->state == RUNNING)
        yield();
}

extern char trampoline[], uservec[], tramp_userret[];  // trampoline.S
extern void kernelvec(void);                            // trapvec.S

// Trap from user mode: uservec (trampoline.S) saved the user registers
// in the trap frame, switched to the kernel page table and stack, and
// jumped here with interrupts off.
void usertrap(void)
{
    struct proc *p = myproc();
    struct trapframe *tf = p->trapframe;
    int dev = DEV_NONE;

    if (csr_read(sstatus) & SSTATUS_SPP)
        panic("usertrap: not from user mode");

    // in the kernel now: traps go to kernelvec.
    csr_write(stvec, (uint64)kernelvec);

    tf->epc = csr_read(sepc);
    tf->cause = csr_read(scause);
    tf->tval = csr_read(stval);

    if (tf->cause == 8) {
        // system call

        if (killed(p))
            exit(-1);

        // sepc points to the ecall instruction, but we want to return
        // to the next instruction.
        tf->epc += 4;

        // an interrupt will change sepc, scause, and sstatus, so
        // enable only now that we're done with those registers.
        intr_on();

        syscall();
    } else if ((dev = devintr(tf->cause)) != DEV_NONE) {
        // ok
    } else {
        // a fault in the user program: kill it, not the kernel
        printf("usertrap(): %s (scause 0x%lx) pid=%d %s\n            sepc=%p stval=%p\n",
               cause_name(tf->cause), tf->cause, p->pid, p->name,
               (void *)tf->epc, (void *)tf->tval);
        setkilled(p);
    }

    if (killed(p))
        exit(-1);

    // give up the CPU if this is a timer interrupt.
    if (dev == DEV_TIMER)
        yield();

    usertrapret();
}

// Return to user mode: through tramp_userret in the trampoline, which
// switches to the user page table and srets.
void usertrapret(void)
{
    struct proc *p = myproc();
    struct trapframe *tf = p->trapframe;

    // we're about to switch the destination of traps from kerneltrap()
    // to usertrap(); interrupts stay off until sret.
    intr_off();

    // send syscalls, interrupts, and exceptions to uservec in the
    // trampoline, at its TRAMPOLINE alias.
    csr_write(stvec, TRAMPOLINE + (uservec - trampoline));

    // what uservec needs next time the process traps into the kernel.
    tf->kernel_satp = MAKE_SATP(V2P(kernel_pagetable));
    tf->kernel_sp = p->kstack + PGSIZE;
    tf->kernel_trap = (uint64)usertrap;
    tf->kernel_hartid = cpuid();

    // sret to user mode (SPP = 0) with interrupts enabled (SPIE).
    uint64 x = csr_read(sstatus);
    x &= ~SSTATUS_SPP;
    x |= SSTATUS_SPIE;
    csr_write(sstatus, x);

    // sret jumps to the user pc.
    csr_write(sepc, tf->epc);

    // code exec() loaded on another hart must be visible here.
    asm volatile("fence.i" ::: "memory");

    // jump to tramp_userret at its TRAMPOLINE alias, which switches to
    // the user page table, restores user registers, and srets.
    extern int asids;
    uint64 satp = MAKE_SATP(V2P(p->pagetable)) | SATP_ASID(asids ? p->asid : 0);
    uint64 fn = TRAMPOLINE + (tramp_userret - trampoline);
    ((void (*)(uint64, uint64))fn)(satp, !asids);
    __builtin_unreachable();
}

// A process's first return to user mode (forkret).
void userret(struct trapframe *tf)
{
    usertrapret();
}
