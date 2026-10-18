// Memory mappings: mmap() and munmap().
//
// A process's mappings (p->vma) lie above its heap: mmap() places them
// downward from the top of user space, and the heap can't grow into
// them (vma_floor()). Pages of a MAP_PRIVATE anonymous mapping are
// allocated when first touched (vma_fault()), and fork() shares them
// copy-on-write. MAP_SHARED pages are all allocated by mmap(), so that
// a child shares them for real.

#include "types.h"
#include "param.h"
#include "memlayout.h"
#include "defs.h"
#include "spinlock.h"
#include "proc.h"
#include "vm.h"
#include "mmu.h"
#include "mman.h"
#include "file.h"

static int prot2perm(int prot) {
    return ((prot & PROT_WRITE) ? PERM_W : 0) | ((prot & PROT_EXEC) ? PERM_X : 0);
}

// The mapping containing va, or 0.
static struct vma *vma_find(struct proc *p, uint64 va) {
    for (int i = 0; i < NVMA; i++) {
        struct vma *v = &p->vma[i];
        if (v->end && v->start <= va && va < v->end)
            return v;
    }
    return 0;
}

// A mapping overlapping [start, end), or 0.
static struct vma *vma_overlap(struct proc *p, uint64 start, uint64 end) {
    for (int i = 0; i < NVMA; i++) {
        struct vma *v = &p->vma[i];
        if (v->end && v->start < end && start < v->end)
            return v;
    }
    return 0;
}

static struct vma *vma_alloc(struct proc *p) {
    for (int i = 0; i < NVMA; i++)
        if (p->vma[i].end == 0)
            return &p->vma[i];
    return 0;
}

// The lowest mapping's start: how far the heap may grow.
uint64 vma_floor(struct proc *p) {
    uint64 floor = USERTOP;
    for (int i = 0; i < NVMA; i++)
        if (p->vma[i].end && p->vma[i].start < floor)
            floor = p->vma[i].start;
    return floor;
}

// Where len bytes fit, as high as possible, below USERTOP and above
// the heap; 0 if nowhere.
static uint64 vma_place(struct proc *p, uint64 len) {
    uint64 top = USERTOP;
    for (;;) {
        if (top < len)
            return 0;
        struct vma *v = vma_overlap(p, top - len, top);
        if (v == 0)
            return top - len >= PGROUNDUP(p->sz) ? top - len : 0;
        top = v->start;
    }
}

// Map a zeroed page at va (in v) in p's page table. Returns its
// physical address, or 0.
static uint64 vma_newpage(struct proc *p, struct vma *v, uint64 va) {
    char *mem = kalloc();
    if (mem == 0)
        return 0;
    memset(mem, 0, PGSIZE);
    if (mappages(p->pagetable, va, PGSIZE, V2P(mem), PTE_USER(prot2perm(v->prot))) != 0) {
        kfree(mem);
        return 0;
    }
    return V2P(mem);
}

// A page fault at va, outside the heap: allocate the page if va is in
// a mapping that may be accessed and has no page there yet. Returns
// the page's physical address, or 0 for a real fault.
uint64 vma_fault(struct proc *p, uint64 va) {
    struct vma *v = vma_find(p, va);
    if (v == 0 || v->prot == PROT_NONE)
        return 0;
    va = PGROUNDDOWN(va);
    pte_t *pte = walk(p->pagetable, va, 0);
    if (pte && (*pte & PTE_VALID))
        return 0;   // there is a page: e.g. a write to a read-only one
    uint64 pa = vma_newpage(p, v, va);
    if (pa)
        asid_flush(p->asid);
    return pa;
}

// Unmap [a, b) of mapping v from pagetable, freeing the pages.
static void vma_unmap_range(pagetable_t pagetable, uint64 a, uint64 b) {
    uvmunmap(pagetable, a, (b - a) / PGSIZE, 1);
}

static void vma_free(struct vma *v) {
    if (v->file)
        fileclose(v->file);
    memset(v, 0, sizeof(*v));
}

// mmap(addr, len, prot, flags, fd, off): map len bytes; addr is only a
// hint, ignored. Returns the mapping's address, or MAP_FAILED (-1).
uint64 sys_mmap(void) {
    struct proc *p = myproc();
    uint64 addr, len, off;
    int prot, flags, fd;

    argaddr(0, &addr);
    argaddr(1, &len);
    argint(2, &prot);
    argint(3, &flags);
    argint(4, &fd);
    argaddr(5, &off);

    int type = flags & (MAP_SHARED | MAP_PRIVATE);
    if (len == 0 || len > USERTOP || (type != MAP_SHARED && type != MAP_PRIVATE) ||
        (flags & ~(MAP_SHARED | MAP_PRIVATE | MAP_ANONYMOUS)) ||
        (prot & ~(PROT_READ | PROT_WRITE | PROT_EXEC)) || off % PGSIZE != 0)
        return -1;
    if (!(flags & MAP_ANONYMOUS))
        return -1;   // files: not yet
    len = PGROUNDUP(len);

    struct vma *v = vma_alloc(p);
    uint64 start = vma_place(p, len);
    if (v == 0 || start == 0)
        return -1;
    v->start = start;
    v->end = start + len;
    v->prot = prot;
    v->flags = flags;
    v->file = 0;
    v->off = 0;

    if (type == MAP_SHARED && prot != PROT_NONE) {
        // all pages now, so that children share them
        for (uint64 va = start; va < v->end; va += PGSIZE) {
            if (vma_newpage(p, v, va) == 0) {
                vma_unmap_range(p->pagetable, start, va);
                vma_free(v);
                return -1;
            }
        }
    }
    return start;
}

// munmap(addr, len): remove the mappings in [addr, addr+len); addr
// must be page aligned. Parts of a mapping may be removed, also from
// the middle, splitting it.
uint64 sys_munmap(void) {
    struct proc *p = myproc();
    uint64 addr, len;

    argaddr(0, &addr);
    argaddr(1, &len);
    uint64 end = addr + PGROUNDUP(len);
    if (addr % PGSIZE != 0 || len == 0 || end < addr || end > USERTOP)
        return -1;

    // splitting a mapping takes a free slot: check first
    int splits = 0, free = 0;
    for (int i = 0; i < NVMA; i++) {
        struct vma *v = &p->vma[i];
        if (v->end == 0)
            free++;
        else if (v->start < addr && end < v->end)
            splits++;
    }
    if (splits > free)
        return -1;

    for (int i = 0; i < NVMA; i++) {
        struct vma *v = &p->vma[i];
        if (v->end == 0 || v->end <= addr || end <= v->start)
            continue;
        uint64 a = addr > v->start ? addr : v->start;
        uint64 b = end < v->end ? end : v->end;
        vma_unmap_range(p->pagetable, a, b);
        if (a == v->start && b == v->end) {
            vma_free(v);
        } else if (a == v->start) {
            v->off += b - v->start;
            v->start = b;
        } else if (b == v->end) {
            v->end = a;
        } else {
            struct vma *w = vma_alloc(p);
            *w = *v;
            w->start = b;
            w->off = v->off + (b - v->start);
            if (w->file)
                filedup(w->file);
            v->end = a;
        }
    }
    asid_flush(p->asid);
    return 0;
}

// fork(): give np p's mappings, MAP_PRIVATE ones copy-on-write.
// Returns 0, or -1 if out of memory (vma_drop() np then).
int vma_fork(struct proc *p, struct proc *np) {
    for (int i = 0; i < NVMA; i++) {
        struct vma *v = &p->vma[i];
        if (v->end == 0)
            continue;
        np->vma[i] = *v;
        if (v->file)
            filedup(v->file);
        int cow = (v->flags & MAP_PRIVATE) != 0;
        if (uvmshare(p->pagetable, np->pagetable, v->start, v->end, cow) < 0)
            return -1;
    }
    return 0;
}

// Undo vma_fork() of a child that never ran.
void vma_drop(struct proc *p) {
    for (int i = 0; i < NVMA; i++) {
        struct vma *v = &p->vma[i];
        if (v->end) {
            vma_unmap_range(p->pagetable, v->start, v->end);
            vma_free(v);
        }
    }
}

// Write MAP_SHARED file mappings back to their files (before exec()).
void vma_sync(struct proc *p) {
}

// exec(): unmap all mappings from the old page table; vma_clear()
// forgets them once exec() has succeeded.
void vma_unmap_pages(struct proc *p, pagetable_t pagetable) {
    for (int i = 0; i < NVMA; i++)
        if (p->vma[i].end)
            vma_unmap_range(pagetable, p->vma[i].start, p->vma[i].end);
}

void vma_clear(struct proc *p) {
    for (int i = 0; i < NVMA; i++)
        if (p->vma[i].end)
            vma_free(&p->vma[i]);
}

// exit(): remove all mappings.
void vma_unmapall(struct proc *p) {
    vma_sync(p);
    vma_drop(p);
    asid_flush(p->asid);
}
