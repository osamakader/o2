// Fatal exceptions taken from EL1. exceptions.S runs these on this
// CPU's emergency stack; they never return.

#include "types.h"
#include "trap.h"
#include "console.h"
#include "backtrace.h"
#include "proc.h"
#include "cpu.h"
#include "vm.h"
#include "mmu.h"
#include "defs.h"

#define ESR_EC(esr) (((esr) >> 26) & 0x3f)

static const char *ec_name(uint64 ec)
{
    switch (ec) {
    case 0x00: return "unknown";
    case 0x0e: return "illegal execution state";
    case 0x15: return "svc";
    case 0x18: return "msr/mrs trap";
    case 0x20: return "instruction abort (lower EL)";
    case 0x21: return "instruction abort";
    case 0x22: return "pc alignment";
    case 0x24: return "data abort (lower EL)";
    case 0x25: return "data abort";
    case 0x26: return "sp alignment";
    case 0x3c: return "brk";
    default:   return "other";
    }
}

void sync_handler(struct trapframe *tf)
{
    uint64 off = 0;
    const char *fn = ksym_lookup(tf->elr, &off);

    panic("sync exception: %s (ec 0x%lx)\n  esr %p elr %p (%s+0x%lx) far %p",
          ec_name(ESR_EC(tf->esr)), ESR_EC(tf->esr), (void *)tf->esr,
          (void *)tf->elr, fn ? fn : "?", off, (void *)tf->far);
}

void fiq_handler(struct trapframe *tf)
{
    panic("FIQ at elr %p", (void *)tf->elr);
}

void serr_handler(struct trapframe *tf)
{
    panic("SError: esr %p elr %p", (void *)tf->esr, (void *)tf->elr);
}

#define EC_SVC64     0x15
#define EC_DABT_LOW  0x24   // data abort from EL0

// Synchronous exception from EL0, on the process's kernel stack with
// interrupts masked.
void usertrap(struct trapframe *tf)
{
    struct proc *p = myproc();

    if (ESR_EC(tf->esr) == EC_SVC64) {
        // system call

        if (killed(p))
            exit(-1);

        // ELR already points past the svc instruction. An interrupt
        // will change registers, so enable only now that we're done
        // with them.
        intr_on();

        syscall();
    } else if (ESR_EC(tf->esr) == EC_DABT_LOW && (tf->esr & 0x3c) == 0x04 &&
               vmfault(p->pagetable, tf->far) != 0) {
        // translation fault (DFSC 0b0001xx) on lazily allocated memory
    } else {
        // a fault in the user program: kill it, not the kernel
        printf("usertrap(): %s (esr %p) pid=%d %s\n            elr=%p far=%p\n",
               ec_name(ESR_EC(tf->esr)), (void *)tf->esr, p->pid, p->name,
               (void *)tf->elr, (void *)tf->far);
        setkilled(p);
    }

    if (killed(p))
        exit(-1);
}

// IRQ from EL0: handle it (a timer tick may yield), then make a killed
// process exit instead of returning to user mode.
void usertrap_irq(struct trapframe *tf)
{
    handle_irq(tf);

    if (killed(myproc()))
        exit(-1);
}

// Last step before returning to EL0 through tf: with interrupts off
// (until eret restores the user's PSTATE), install this process's
// page table on this core.
void usertrapret(struct trapframe *tf)
{
    intr_off();
    uvmswitch(myproc()->pagetable, myproc()->asid);
}
