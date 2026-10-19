// Shared memory: memfd(size) returns a file of size zeroed bytes of
// memory, which every process that maps it (MAP_SHARED) sees, also
// after exec() and through a file passed on. The desktop gives its
// apps their windows' pixels this way. Pages are allocated when first
// mapped, so a big one costs only what is used. read() and write()
// don't work on it; mmap() and close() do.

#include "types.h"
#include "param.h"
#include "memlayout.h"
#include "defs.h"
#include "spinlock.h"
#include "proc.h"
#include "file.h"

#define DIRPAGES (PGSIZE / sizeof(uint64))   // page addresses in a dir page
#define NSHMDIR  8                           // so at most 16MB

struct shm {
    struct spinlock lock;
    uint64 npages;
    uint64 *dir[NSHMDIR];   // pages of the pages' physical addresses
};

// Page n's physical address, allocating it if need be; 0 if n is past
// the end or out of memory.
uint64 shm_page(struct shm *s, uint64 n) {
    if (n >= s->npages)
        return 0;
    acquire(&s->lock);
    uint64 **dir = &s->dir[n / DIRPAGES];
    if (*dir == 0 && (*dir = kalloc()) != 0)
        memset(*dir, 0, PGSIZE);
    uint64 pa = 0;
    if (*dir) {
        uint64 *slot = &(*dir)[n % DIRPAGES];
        char *mem;
        if (*slot == 0 && (mem = kalloc()) != 0) {
            memset(mem, 0, PGSIZE);
            *slot = V2P(mem);
        }
        pa = *slot;
    }
    release(&s->lock);
    return pa;
}

void shm_free(struct shm *s) {
    for (int i = 0; i < NSHMDIR; i++) {
        if (s->dir[i] == 0)
            continue;
        for (int j = 0; j < DIRPAGES; j++)
            if (s->dir[i][j])
                kfree((void *)P2V(s->dir[i][j]));
        kfree(s->dir[i]);
    }
    kfree(s);
}

// memfd(size): a file of shared memory. Returns its fd, or -1.
uint64 sys_memfd(void) {
    uint64 size;
    argaddr(0, &size);
    uint64 npages = PGROUNDUP(size) / PGSIZE;
    if (npages == 0 || npages > NSHMDIR * DIRPAGES)
        return -1;
    struct shm *s = kalloc();
    if (s == 0)
        return -1;
    memset(s, 0, PGSIZE);
    initlock(&s->lock, "shm");
    s->npages = npages;

    struct file *f = filealloc();
    int fd;
    if (f == 0 || (fd = fdalloc(f)) < 0) {
        if (f)
            fileclose(f);   // FD_NONE: just freed
        shm_free(s);
        return -1;
    }
    f->type = FD_SHM;
    f->readable = 1;
    f->writable = 1;
    f->shm = s;
    return fd;
}
