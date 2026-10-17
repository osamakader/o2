// The xv6 riscv.h definitions xv6 user tests use, with o2's values on
// both arches.

#ifndef XV6_COMPAT_RISCV_H
#define XV6_COMPAT_RISCV_H

#include "types.h"

#define PGSIZE 4096
#define PGROUNDUP(sz)  (((sz)+PGSIZE-1) & ~(PGSIZE-1))
#define PGROUNDDOWN(a) (((a)) & ~(PGSIZE-1))

#if defined(__aarch64__)
// User space is the whole TTBR0 range; there is no trap frame page in
// it, so user memory may extend all the way to MAXVA.
#define MAXVA      (1L << 39)
#define TRAPFRAME  MAXVA
#define TRAMPOLINE MAXVA
#else
// Sv39 user half, with xv6's trampoline and trap frame pages on top.
#define MAXVA      (1L << 38)
#define TRAMPOLINE (MAXVA - PGSIZE)
#define TRAPFRAME  (TRAMPOLINE - PGSIZE)
#endif

static inline uint64 r_sp(void)
{
  uint64 x;
#if defined(__aarch64__)
  asm volatile("mov %0, sp" : "=r"(x));
#else
  asm volatile("mv %0, sp" : "=r"(x));
#endif
  return x;
}

#endif
