// Traps: interrupts and exceptions from the kernel and from user mode.

#include "types.h"
#include "param.h"
#include "memlayout.h"
#include "console.h"
#include "proc.h"
#include "cpu.h"
#include "trap.h"

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

// Interrupt in the kernel, on the current kernel stack.
void kerneltrap(struct trapframe *tf)
{
    if ((tf->status & SSTATUS_SPP) == 0)
        panic("kerneltrap: not from supervisor mode");

    printf("unexpected interrupt scause 0x%lx on cpu %d\n", tf->cause, cpuid());
}
