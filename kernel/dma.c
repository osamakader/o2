// Memory for devices that read and write it themselves (DMA), like the
// USB controller: zeroed pages, which the CPU uses through an alias at
// physical + DMA_OFFSET (uncached on arm64: see mmu.h). They are never
// freed.

#include "types.h"
#include "param.h"
#include "memlayout.h"
#include "defs.h"
#include "vm.h"
#include "mmu.h"

void *dma_alloc(void) {
    char *p = kalloc();
    if (p == 0)
        return 0;
    memset(p, 0, PGSIZE);
    // out of the caches, so that no dirty line of the cached alias is
    // written back over what the device writes
    for (int i = 0; i < PGSIZE; i += 64)
        dcache_flush_line((uint64)p + i);
    uint64 pa = V2P(p);
    dma_map(pa);
    return (void *)(pa + DMA_OFFSET);
}

// The physical address (the device's) of DMA memory at va.
uint64 dma_pa(void *va) {
    return (uint64)va - DMA_OFFSET;
}
