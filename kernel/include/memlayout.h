// Memory layout. Kept free of C-only syntax in the plain constants so
// assembly can include it.

#include "param.h"

// device addresses, IRQs and KERNBASE (start of RAM) for this machine
#include "platform.h"

// the kernel expects there to be RAM
// for use by the kernel and user pages
// from physical address KERNBASE to PHYSTOP.
#define PHYSTOP (KERNBASE + 128*1024*1024)


#define PGSIZE 4096 // bytes per page

// The kernel runs in the high half and reaches all of RAM and the
// devices through a linear map: virtual = physical + KVA_OFFSET.
// 0xFFFFFFC0_00000000 is valid for both arm64 TTBR1 with a 39-bit VA
// and the upper half of riscv64 Sv39. kernel.ld repeats this value.
#define KVA_OFFSET 0xFFFFFFC000000000
#define P2V(pa) ((uint64)(pa) + KVA_OFFSET)
#define V2P(va) ((uint64)(va) - KVA_OFFSET)

// Kernel stacks live at the top of the kernel address space, one per
// ptable slot, each with an unmapped guard page below it so an
// overflow faults instead of silently corrupting memory.
#define KSTACK(i) (0 - (uint64)(((i) + 1) * 2 * PGSIZE))

#define PGROUNDUP(sz)  (((sz)+PGSIZE-1) & ~(PGSIZE-1))
#define PGROUNDDOWN(a) (((a)) & ~(PGSIZE-1))