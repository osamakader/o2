// Load an ELF program into a process.

#include "types.h"
#include "param.h"
#include "memlayout.h"
#include "defs.h"
#include "console.h"
#include "proc.h"
#include "vm.h"
#include "elf.h"
#include "exec.h"
#include "cpu.h"

static int memread(struct elfsrc *src, uint64 off, void *dst, uint64 n) {
    if (off > src->len || n > src->len - off)
        return -1;
    memmove(dst, src->img + off, n);
    return n;
}

void elfsrc_mem(struct elfsrc *src, const uchar *img, uint64 len) {
    src->read = memread;
    src->img = img;
    src->len = len;
}

static int flags2perm(int flags) {
    int perm = 0;
    if (flags & ELF_PROG_FLAG_EXEC)
        perm |= PERM_X;
    if (flags & ELF_PROG_FLAG_WRITE)
        perm |= PERM_W;
    return perm;
}

// Load a program segment into pagetable at virtual address va.
// va must be page-aligned and the pages from va to va+sz must already
// be mapped. Returns 0 on success, -1 on failure.
static int loadseg(pagetable_t pagetable, uint64 va, struct elfsrc *src, uint64 off, uint64 sz) {
    for (uint64 i = 0; i < sz; i += PGSIZE) {
        uint64 pa = walkaddr(pagetable, va + i);
        if (pa == 0)
            panic("loadseg: address should exist");
        uint64 n = sz - i < PGSIZE ? sz - i : PGSIZE;
        if (src->read(src, off + i, (void *)P2V(pa), n) != n)
            return -1;
        // the program may execute what was just written
        sync_icache(P2V(pa), n);
    }
    return 0;
}

// Replace p's user memory with the ELF program read from src, passing
// argv (kernel strings, null-terminated list). p must not be running
// in user mode; on success it starts the new program the next time it
// returns there. Returns argc, which also lands in x0, or -1.
int exec(struct proc *p, const char *name, struct elfsrc *src, char **argv) {
    struct elfhdr elf;
    struct proghdr ph;
    uint64 argc, sz = 0, sp, ustack[MAXARG], stackbase;
    pagetable_t pagetable = uvmcreate();

    if (pagetable == 0)
        return -1;

    // Check ELF header
    if (src->read(src, 0, &elf, sizeof(elf)) != sizeof(elf))
        goto bad;
    if (elf.magic != ELF_MAGIC || elf.machine != ELF_MACHINE)
        goto bad;

    // Load program into memory.
    for (int i = 0; i < elf.phnum; i++) {
        uint64 off = elf.phoff + i * sizeof(ph);
        if (src->read(src, off, &ph, sizeof(ph)) != sizeof(ph))
            goto bad;
        if (ph.type != ELF_PROG_LOAD || ph.memsz == 0)
            continue;
        if (ph.memsz < ph.filesz)
            goto bad;
        if (ph.vaddr + ph.memsz < ph.vaddr)
            goto bad;
        if (ph.vaddr % PGSIZE != 0)
            goto bad;
        uint64 sz1 = uvmalloc(pagetable, sz, ph.vaddr + ph.memsz, flags2perm(ph.flags));
        if (sz1 == 0)
            goto bad;
        sz = sz1;
        if (loadseg(pagetable, ph.vaddr, src, ph.off, ph.filesz) < 0)
            goto bad;
    }

    // Allocate some pages at the next page boundary.
    // Make the first inaccessible as a stack guard.
    // Use the rest as the user stack.
    sz = PGROUNDUP(sz);
    uint64 sz1 = uvmalloc(pagetable, sz, sz + (USERSTACK + 1) * PGSIZE, PERM_W);
    if (sz1 == 0)
        goto bad;
    sz = sz1;
    uvmclear(pagetable, sz - (USERSTACK + 1) * PGSIZE);
    sp = sz;
    stackbase = sp - USERSTACK * PGSIZE;

    // Push argument strings, prepare rest of stack in ustack.
    for (argc = 0; argv[argc]; argc++) {
        if (argc >= MAXARG)
            goto bad;
        sp -= strlen(argv[argc]) + 1;
        sp -= sp % 16;  // the arm64 ABI wants a 16-byte aligned sp
        if (sp < stackbase)
            goto bad;
        if (copyout(pagetable, sp, argv[argc], strlen(argv[argc]) + 1) < 0)
            goto bad;
        ustack[argc] = sp;
    }
    ustack[argc] = 0;

    // push the array of argv[] pointers.
    sp -= (argc + 1) * sizeof(uint64);
    sp -= sp % 16;
    if (sp < stackbase)
        goto bad;
    if (copyout(pagetable, sp, (char *)ustack, (argc + 1) * sizeof(uint64)) < 0)
        goto bad;

    // arguments to user main(argc, argv)
    TF_ARG(p->trapframe, 0) = argc;
    TF_ARG(p->trapframe, 1) = sp;

    // Save program name for debugging.
    safestrcpy(p->name, name, sizeof(p->name));

    // Commit to the user image.
    pagetable_t oldpagetable = p->pagetable;
    uint64 oldsz = p->sz;
    p->pagetable = pagetable;
    p->sz = sz;
    tf_init_user(p->trapframe, elf.entry, sp);  // start at _start
    if (oldpagetable)
        uvmfree(oldpagetable, oldsz);

    return argc;

bad:
    uvmfree(pagetable, sz);
    return -1;
}
