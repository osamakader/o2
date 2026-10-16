// Physical memory layout. Kept free of C-only syntax in the plain
// constants so assembly can include it.

#include "param.h"

// qemu puts UART registers here in physical memory.
#define UART0 0x9000000

// qemu puts interrupt controller (GICv3) here.
#define GICv3 0x08000000

// GICv3 distributor, and one 128KB redistributor (RD + SGI frames)
// per CPU starting at GICR.
#define GICD      GICv3
#define GICD_SIZE 0x10000
#define GICR      (GICv3 + 0xA0000)
#define GICR_SIZE (NCPU * 0x20000)


// the kernel expects there to be RAM
// for use by the kernel and user pages
// from physical address 0x40000000 to PHYSTOP.
#define KERNBASE 0x40000000
#define PHYSTOP (KERNBASE + 128*1024*1024)


#define PGSIZE 4096 // bytes per page

// Kernel stacks live at the top of the address space, one per ptable
// slot, each with an unmapped guard page below it so an overflow
// faults instead of silently corrupting memory.
#define KSTACK(i) (MAXVA - ((i) + 1) * 2 * PGSIZE)

#define PGROUNDUP(sz)  (((sz)+PGSIZE-1) & ~(PGSIZE-1))
#define PGROUNDDOWN(a) (((a)) & ~(PGSIZE-1))