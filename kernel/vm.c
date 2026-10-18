#include "types.h"
#include "memlayout.h"
#include "defs.h"
#include "board.h"
#include "console.h"
#include "vm.h"
#include "mmu.h"
#include "proc.h"

// the kernel's page table: RAM and devices at P2V(pa).
pagetable_t kernel_pagetable;

extern char stext[];  // kernel.ld: start of kernel text
extern char etext[];  // kernel.ld: first page after kernel text
extern char sdata[];  // kernel.ld: first page of writable data

// Return the address of the leaf PTE for va, allocating the
// intermediate tables if alloc is set. Returns 0 if a table is
// missing and alloc is clear, or if allocation fails.
pte_t *walk(pagetable_t pagetable, uint64 va, int alloc) {
    if (!VA_VALID(va))
        panic("walk");

    for (int level = 2; level > 0; level--) {
        pte_t *pte = &pagetable[PX(level, va)];
        if (*pte & PTE_VALID) {
            pagetable = (pagetable_t)P2V(PTE2PA(*pte));
        } else {
            if (!alloc || (pagetable = (pagetable_t)kalloc()) == 0)
                return 0;
            memset(pagetable, 0, PGSIZE);
            *pte = MKTABLE(V2P(pagetable));
        }
    }
    return &pagetable[PX(0, va)];
}

// Map [va, va+size) to physical addresses starting at pa. va and size
// need not be page aligned. Returns 0 on success, -1 if walk() could
// not allocate a page-table page.
int mappages(pagetable_t pagetable, uint64 va, uint64 size, uint64 pa, uint64 attrs) {
    if (size == 0)
        panic("mappages: size");

    uint64 a = PGROUNDDOWN(va);
    uint64 last = PGROUNDDOWN(va + size - 1);
    for (;;) {
        pte_t *pte = walk(pagetable, a, 1);
        if (pte == 0)
            return -1;
        if (*pte & PTE_VALID)
            panic("mappages: remap");
        *pte = MKPAGE(pa, attrs);
        if (a == last)
            break;
        a += PGSIZE;
        pa += PGSIZE;
    }
    return 0;
}

void kvmmap(pagetable_t pagetable, uint64 va, uint64 pa, uint64 size, uint64 attrs) {
    if (mappages(pagetable, va, size, pa, attrs) != 0)
        panic("kvmmap");
}

static pagetable_t kvmmake(void) {
    pagetable_t pagetable = (pagetable_t)kalloc();
    if (!pagetable)
        panic("kvmmake");
    memset(pagetable, 0, PGSIZE);

    // device registers
    kvmmap_devices(pagetable);

    // RAM below the kernel image (DTB, firmware)
    kvmmap(pagetable, P2V(KERNBASE), KERNBASE, V2P(stext) - KERNBASE, PTE_KERN_DATA);

    // kernel text: read-only, executable
    kvmmap(pagetable, (uint64)stext, V2P(stext), etext - stext, PTE_KERN_TEXT);

    // read-only data
    kvmmap(pagetable, (uint64)etext, V2P(etext), sdata - etext, PTE_KERN_RODATA);

    // kernel data and the rest of RAM: read/write, never executable
    kvmmap(pagetable, (uint64)sdata, V2P(sdata), board.ram_end - V2P(sdata), PTE_KERN_DATA);

    // a guarded kernel stack for each ptable slot
    proc_mapstacks(pagetable);

    return pagetable;
}

void kvminit(void) {
    kernel_pagetable = kvmmake();
}

//
// User address spaces: [0, sz) mapped from virtual address 0.
//

// Look up a user virtual address, return the physical address, or 0
// if not mapped or not accessible from user mode.
uint64 walkaddr(pagetable_t pagetable, uint64 va) {
    if (va >= MAXVA)
        return 0;

    pte_t *pte = walk(pagetable, va, 0);
    if (pte == 0 || (*pte & PTE_VALID) == 0 || !PTE_IS_USER(*pte))
        return 0;
    return PTE2PA(*pte);
}

// Remove npages of mappings starting from va, which must be page
// aligned; pages never mapped (lazy allocation) are skipped. Optionally
// free the physical memory.
void uvmunmap(pagetable_t pagetable, uint64 va, uint64 npages, int do_free) {
    if ((va % PGSIZE) != 0)
        panic("uvmunmap: not aligned");

    for (uint64 a = va; a < va + npages * PGSIZE; a += PGSIZE) {
        pte_t *pte = walk(pagetable, a, 0);
        if (pte == 0 || (*pte & PTE_VALID) == 0)
            continue;  // never touched lazily allocated memory
        if (do_free)
            kfree((void *)P2V(PTE2PA(*pte)));
        *pte = 0;
    }
}

// Create an empty user page table. Returns 0 if out of memory.
pagetable_t uvmcreate(void) {
    pagetable_t pagetable = (pagetable_t)kalloc();
    if (pagetable == 0)
        return 0;
    memset(pagetable, 0, PGSIZE);
    uvminit_root(pagetable);
    return pagetable;
}

// Allocate zeroed pages to grow a process from oldsz to newsz, which
// need not be page aligned. Returns the new size or 0 on error.
uint64 uvmalloc(pagetable_t pagetable, uint64 oldsz, uint64 newsz, int perm) {
    if (newsz < oldsz)
        return oldsz;

    oldsz = PGROUNDUP(oldsz);
    for (uint64 a = oldsz; a < newsz; a += PGSIZE) {
        char *mem = kalloc();
        if (mem == 0) {
            uvmdealloc(pagetable, a, oldsz);
            return 0;
        }
        memset(mem, 0, PGSIZE);
        if (mappages(pagetable, a, PGSIZE, V2P(mem), PTE_USER(perm)) != 0) {
            kfree(mem);
            uvmdealloc(pagetable, a, oldsz);
            return 0;
        }
    }
    return newsz;
}

// Deallocate user pages to bring the process size from oldsz to
// newsz. oldsz and newsz need not be page-aligned, nor does newsz need
// to be less than oldsz. oldsz can be larger than the actual process
// size. Returns the new process size.
uint64 uvmdealloc(pagetable_t pagetable, uint64 oldsz, uint64 newsz) {
    if (newsz >= oldsz)
        return oldsz;

    if (PGROUNDUP(newsz) < PGROUNDUP(oldsz)) {
        uint64 npages = (PGROUNDUP(oldsz) - PGROUNDUP(newsz)) / PGSIZE;
        uvmunmap(pagetable, PGROUNDUP(newsz), npages, 1);
    }
    return newsz;
}

// Recursively free page-table pages below level. All leaf mappings
// must already have been removed. In the root, only the user entries:
// any others are the kernel's, shared by every user root.
static void freewalk(pagetable_t pagetable, int level) {
    int n = level == 2 ? UVM_ROOT_ENTRIES : 512;

    for (int i = 0; i < n; i++) {
        pte_t pte = pagetable[i];
        if ((pte & PTE_VALID) == 0)
            continue;
        if (level == 0)
            panic("freewalk: leaf");
        freewalk((pagetable_t)P2V(PTE2PA(pte)), level - 1);
        pagetable[i] = 0;
    }
    kfree(pagetable);
}

// Free user memory pages, then free page-table pages.
void uvmfree(pagetable_t pagetable, uint64 sz) {
    if (sz > 0)
        uvmunmap(pagetable, 0, PGROUNDUP(sz) / PGSIZE, 1);
    freewalk(pagetable, 2);
}

// Map the pages old maps in [start, end) (page aligned) into new too.
// With cow, writable ones become copy-on-write in both, so that the
// first write to one gives the writer a copy of its own (uvmcow());
// without, both page tables share them as they are. The caller must
// flush the old page table's TLB entries. Returns 0 on success, -1 if
// out of memory for page-table pages, after undoing new's mappings.
int uvmshare(pagetable_t old, pagetable_t new, uint64 start, uint64 end, int cow) {
    uint64 i;

    for (i = start; i < end; i += PGSIZE) {
        pte_t *pte = walk(old, i, 0);
        if (pte == 0 || (*pte & PTE_VALID) == 0)
            continue;  // not allocated yet (lazy): nothing to share
        if (cow && PTE_IS_USER_W(*pte))
            *pte = PTE_MAKE_COW(*pte);
        uint64 pa = PTE2PA(*pte);
        if (mappages(new, i, PGSIZE, pa, PTE_FLAGS(*pte)) != 0)
            goto err;
        kref((void *)P2V(pa));
    }
    return 0;

err:
    uvmunmap(new, start, (i - start) / PGSIZE, 1);
    return -1;
}

// Give a child (fork()) the parent's memory [0, sz), copy-on-write.
int uvmcopy(pagetable_t old, pagetable_t new, uint64 sz) {
    return uvmshare(old, new, 0, PGROUNDUP(sz), 1);
}

// A write to va in the current process, whose page table is pagetable,
// hit a copy-on-write page: give the process a copy of the page, or if
// no other page table maps it any more, the page itself, and make it
// writable. Returns the page's physical address, or 0 if va isn't a
// copy-on-write page or memory ran out.
uint64 uvmcow(pagetable_t pagetable, uint64 va) {
    if (va >= MAXVA)
        return 0;
    pte_t *pte = walk(pagetable, PGROUNDDOWN(va), 0);
    if (pte == 0 || (*pte & PTE_VALID) == 0 || !PTE_IS_COW(*pte))
        return 0;

    uint64 pa = PTE2PA(*pte);
    if (krefs((void *)P2V(pa)) > 1) {
        char *mem = kalloc();
        if (mem == 0)
            return 0;
        memmove(mem, (char *)P2V(pa), PGSIZE);
        *pte = PA2PTE(V2P(mem)) | PTE_FLAGS(*pte);
        kfree((void *)P2V(pa));
        pa = V2P(mem);
    }
    *pte = PTE_UNCOW(*pte);
    // the read-only entry may be in the TLB
    asid_flush(myproc()->asid);
    return pa;
}

// Mark a PTE invalid for user access. Used by exec for the user stack
// guard page.
void uvmclear(pagetable_t pagetable, uint64 va) {
    pte_t *pte = walk(pagetable, va, 0);
    if (pte == 0)
        panic("uvmclear");
    *pte = PTE_CLEAR_USER(*pte);
}

// Lazily allocated memory (sbrklazy, or mmap above the heap): map a
// zeroed user page at va in the current process's page table if va is
// below its size and nothing is mapped there yet. Returns the page's
// physical address, or 0.
uint64 vmfault(pagetable_t pagetable, uint64 va) {
    struct proc *p = myproc();

    if (p == 0 || p->pagetable != pagetable || va >= MAXVA)
        return 0;
    if (va >= p->sz)
        return vma_fault(p, va);
    va = PGROUNDDOWN(va);
    pte_t *pte = walk(pagetable, va, 0);
    if (pte && (*pte & PTE_VALID))
        return 0;  // mapped: a real fault, e.g. the stack guard page

    char *mem = kalloc();
    if (mem == 0)
        return 0;
    memset(mem, 0, PGSIZE);
    if (mappages(pagetable, va, PGSIZE, V2P(mem), PTE_USER(PERM_W)) != 0) {
        kfree(mem);
        return 0;
    }
    // riscv64 harts may have cached the invalid entry
    asid_flush(p->asid);
    return V2P(mem);
}

// Copy from kernel to user. Copy len bytes from src to virtual address
// dstva in a given page table. Return 0 on success, -1 on error.
int copyout(pagetable_t pagetable, uint64 dstva, const char *src, uint64 len) {
    while (len > 0) {
        uint64 va0 = PGROUNDDOWN(dstva);
        if (va0 >= MAXVA)
            return -1;
        pte_t *pte = walk(pagetable, va0, 0);
        if ((pte == 0 || (*pte & PTE_VALID) == 0) && vmfault(pagetable, va0) != 0)
            pte = walk(pagetable, va0, 0);
        if (pte && (*pte & PTE_VALID) && PTE_IS_COW(*pte) && uvmcow(pagetable, va0) == 0)
            return -1;
        if (pte == 0 || (*pte & PTE_VALID) == 0 || !PTE_IS_USER_W(*pte))
            return -1;
        uint64 n = PGSIZE - (dstva - va0);
        if (n > len)
            n = len;
        memmove((char *)P2V(PTE2PA(*pte)) + (dstva - va0), src, n);

        len -= n;
        src += n;
        dstva = va0 + PGSIZE;
    }
    return 0;
}

// Copy from user to kernel. Copy len bytes to dst from virtual address
// srcva in a given page table. Return 0 on success, -1 on error.
int copyin(pagetable_t pagetable, char *dst, uint64 srcva, uint64 len) {
    while (len > 0) {
        uint64 va0 = PGROUNDDOWN(srcva);
        uint64 pa0 = walkaddr(pagetable, va0);
        if (pa0 == 0 && (pa0 = vmfault(pagetable, va0)) == 0)
            return -1;
        uint64 n = PGSIZE - (srcva - va0);
        if (n > len)
            n = len;
        memmove(dst, (char *)P2V(pa0) + (srcva - va0), n);

        len -= n;
        dst += n;
        srcva = va0 + PGSIZE;
    }
    return 0;
}

// Copy a null-terminated string from user to kernel. Copy bytes to dst
// from virtual address srcva in a given page table, until a '\0', or
// max. Return 0 on success, -1 on error.
int copyinstr(pagetable_t pagetable, char *dst, uint64 srcva, uint64 max) {
    int got_null = 0;

    while (got_null == 0 && max > 0) {
        uint64 va0 = PGROUNDDOWN(srcva);
        uint64 pa0 = walkaddr(pagetable, va0);
        if (pa0 == 0 && (pa0 = vmfault(pagetable, va0)) == 0)
            return -1;
        uint64 n = PGSIZE - (srcva - va0);
        if (n > max)
            n = max;

        char *p = (char *)P2V(pa0) + (srcva - va0);
        while (n > 0) {
            if (*p == '\0') {
                *dst = '\0';
                got_null = 1;
                break;
            }
            *dst = *p;
            --n;
            --max;
            p++;
            dst++;
        }

        srcva = va0 + PGSIZE;
    }
    return got_null ? 0 : -1;
}
