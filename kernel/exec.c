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
#include "fs.h"
#include "file.h"
#include "cpu.h"
#include "mmu.h"
#include "fpu.h"

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

static int inoderead(struct elfsrc *src, uint64 off, void *dst, uint64 n) {
    if (off + n > src->ip->size)
        return -1;
    return readi(src->ip, 0, (uint64)dst, off, n);
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
    pagetable_t pagetable = proc_pagetable(p);

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
        if (ph.vaddr + ph.memsz > USERTOP)
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
    if (sz + (USERSTACK + 1) * PGSIZE > USERTOP)
        goto bad;
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
        sp -= sp % 16;  // arm64 and riscv64 ABIs want a 16-byte aligned sp
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
    fp_exec(p);
    sigexec(p);
    if (oldpagetable) {
        vma_unmap_pages(p, oldpagetable);
        // This hart may still be translating through the old table (on
        // riscv64 its upper half maps the kernel itself); leave it before
        // kfree() fills it with junk. usertrapret() installs the new one.
        if (p == myproc())
            kvmswitch();
        proc_freepagetable(oldpagetable, oldsz);
        // the new page table reuses p's ASID
        asid_flush(p->asid);
    }

    return argc;

bad:
    proc_freepagetable(pagetable, sz);
    return -1;
}

// A script starts with "#!interpreter [arg]". Running it runs the
// interpreter instead, with argv: interpreter, arg (if any), the
// script's path, then the script's argv[1..]. The interpreter itself
// must be an ELF program. Parses ip's first line into line and fills
// in nargv; returns 0 if ip isn't a script, 1 if it is, -1 if it is
// one we can't run.
static int script(struct inode *ip, char *line, int max, char *path,
                  char **argv, char **nargv) {
    int n = readi(ip, 0, (uint64)line, 0, max - 1);
    if (n < 2 || line[0] != '#' || line[1] != '!')
        return 0;
    line[n] = 0;
    char *s = line + 2, *interp, *arg = 0;
    while (*s == ' ' || *s == '\t')
        s++;
    interp = s;
    while (*s && *s != ' ' && *s != '\t' && *s != '\n')
        s++;
    if (*s == ' ' || *s == '\t') {
        // the rest of the line is one argument, as on Linux
        *s++ = 0;
        while (*s == ' ' || *s == '\t')
            s++;
        if (*s && *s != '\n')
            arg = s;
        while (*s && *s != '\n')
            s++;
    }
    if (*s != '\n' && n == max - 1)
        return -1;   // no end of line within max: too long
    *s = 0;
    // trailing blanks after arg
    while (arg && s > arg && (s[-1] == ' ' || s[-1] == '\t'))
        *--s = 0;
    if (*interp == 0)
        return -1;

    int i = 0;
    nargv[i++] = interp;
    if (arg)
        nargv[i++] = arg;
    nargv[i++] = path;
    for (int j = 1; argv[0] && argv[j]; j++) {
        if (i >= MAXARG)
            return -1;
        nargv[i++] = argv[j];
    }
    nargv[i] = 0;
    return 1;
}

// exec() system call: replace the current process's image with the
// program at path, read from the file system.
int kexec(char *path, char **argv) {
    struct elfsrc src = { .read = inoderead };
    char *name, *s;
    char line[MAXPATH], *nargv[MAXARG + 1];

    // the new program won't have the old one's mappings: write shared
    // ones back now, outside exec's transaction
    vma_sync(myproc());

    begin_op();
    if ((src.ip = namei(path)) == 0) {
        end_op();
        return -1;
    }
    ilock(src.ip);

    // program name for debugging: the last path element (of the
    // script, not its interpreter)
    for (name = s = path; *s; s++)
        if (*s == '/')
            name = s + 1;

    int r = script(src.ip, line, sizeof(line), path, argv, nargv);
    if (r != 0) {
        iunlockput(src.ip);
        if (r < 0 || (src.ip = namei(nargv[0])) == 0) {
            end_op();
            return -1;
        }
        ilock(src.ip);
        argv = nargv;
    }

    r = exec(myproc(), name, &src, argv);

    iunlockput(src.ip);
    end_op();
    if (r >= 0)
        vma_clear(myproc());   // may close files: after the transaction
    return r;
}
