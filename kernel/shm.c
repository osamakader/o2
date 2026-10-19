// Shared memory: memfd(size) returns a file of size zeroed bytes of
// memory, which every process that maps it (MAP_SHARED) sees, also
// after exec() and through a file passed on. The desktop gives its
// apps their windows' pixels this way. read() and write() don't work on
// it; mmap() and close() do.

#include "types.h"
#include "param.h"
#include "memlayout.h"
#include "defs.h"
#include "spinlock.h"
#include "proc.h"
#include "file.h"

#define DIRPAGES (PGSIZE / sizeof(uint64))   // page addresses in a dir page
#define NSHMDIR  4                           // so at most 8MB

struct shm {
    uint64 npages;
    uint64 *dir[NSHMDIR];   // pages of the pages' physical addresses
};

// Page n's physical address.
uint64 shm_page(struct shm *s, uint64 n) {
    if (n >= s->npages)
        return 0;
    return s->dir[n / DIRPAGES][n % DIRPAGES];
}

void shm_free(struct shm *s) {
    for (uint64 n = 0; n < s->npages; n++)
        kfree((void *)P2V(shm_page(s, n)));
    for (int i = 0; i < NSHMDIR; i++)
        if (s->dir[i])
            kfree(s->dir[i]);
    kfree(s);
}

static struct shm *shm_alloc(uint64 size) {
    uint64 npages = PGROUNDUP(size) / PGSIZE;
    if (npages == 0 || npages > NSHMDIR * DIRPAGES)
        return 0;
    struct shm *s = kalloc();
    if (s == 0)
        return 0;
    memset(s, 0, PGSIZE);
    for (uint64 n = 0; n < npages; n++) {
        if (n % DIRPAGES == 0 && (s->dir[n / DIRPAGES] = kalloc()) == 0)
            goto bad;
        char *mem = kalloc();
        if (mem == 0)
            goto bad;
        memset(mem, 0, PGSIZE);
        s->dir[n / DIRPAGES][n % DIRPAGES] = V2P(mem);
        s->npages = n + 1;
    }
    return s;

bad:
    shm_free(s);
    return 0;
}

// memfd(size): a file of shared memory. Returns its fd, or -1.
uint64 sys_memfd(void) {
    uint64 size;
    argaddr(0, &size);
    struct shm *s = shm_alloc(size);
    if (s == 0)
        return -1;
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
