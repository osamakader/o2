// Memory layout. Kept free of C-only syntax in the plain constants so
// assembly can include it.

#include "param.h"

// device addresses, IRQs and KERNBASE (start of RAM) for this machine
#include "platform.h"

// RAM starts at KERNBASE; how much there is comes from the device tree
// (board.ram_end), or else is DEFAULT_RAM. Up to MAX_RAM is used. The
// boot page tables (entry.S) map the first BOOT_RAM_GB (platform.h) in
// 1GB blocks: kinit() frees that, kinit_rest() the rest once the
// kernel page table is installed.
#define DEFAULT_RAM (128UL*1024*1024)
#define MAX_RAM     (16UL*1024*1024*1024)
#define BOOT_RAM    (BOOT_RAM_GB * 1024UL*1024*1024)


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