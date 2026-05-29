// Physical memory layout

// qemu puts UART registers here in physical memory.
#define UART0 0x9000000

// qemu puts interrupt controller (GICv3) here.
#define GICv3 0x08000000


// the kernel expects there to be RAM
// for use by the kernel and user pages
// from physical address 0x40000000 to PHYSTOP.
#define KERNBASE 0x40000000L
#define PHYSTOP (KERNBASE + 128*1024*1024)


#define PGSIZE 4096 // bytes per page

#define PGROUNDUP(sz)  (((sz)+PGSIZE-1) & ~(PGSIZE-1))
#define PGROUNDDOWN(a) (((a)) & ~(PGSIZE-1))