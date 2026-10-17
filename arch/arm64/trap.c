// Fatal exceptions taken from EL1. exceptions.S runs these on this
// CPU's emergency stack; they never return.

#include "types.h"
#include "trap.h"
#include "console.h"
#include "proc.h"
#include "cpu.h"
#include "vm.h"

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
    panic("sync exception: %s (ec 0x%lx)\n  esr %p elr %p far %p",
          ec_name(ESR_EC(tf->esr)), ESR_EC(tf->esr), (void *)tf->esr,
          (void *)tf->elr, (void *)tf->far);
}

void fiq_handler(struct trapframe *tf)
{
    panic("FIQ at elr %p", (void *)tf->elr);
}

void serr_handler(struct trapframe *tf)
{
    panic("SError: esr %p elr %p", (void *)tf->esr, (void *)tf->elr);
}

// Synchronous exception from EL0. Syscalls are not dispatched yet.
void usertrap(struct trapframe *tf)
{
    if (ESR_EC(tf->esr) == 0x15)
        panic("usertrap: syscall %ld from pid %d", (long)tf->x[8], myproc()->pid);
    panic("usertrap: %s esr %p elr %p far %p", ec_name(ESR_EC(tf->esr)),
          (void *)tf->esr, (void *)tf->elr, (void *)tf->far);
}

// Last step before returning to EL0 through tf: with interrupts off
// (until eret restores the user's PSTATE), install this process's
// page table on this core.
void usertrapret(struct trapframe *tf)
{
    intr_off();
    uvmswitch(myproc()->pagetable);
}
