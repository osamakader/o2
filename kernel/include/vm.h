#ifndef VM_H
#define VM_H

#include "types.h"

// Page-table geometry shared by arm64 (4KB granule, 39-bit VA) and
// riscv64 Sv39: three levels of 512 entries, 9 VA bits per level.
// Level 2 is the root, level 0 holds the leaf page entries. The arch's
// mmu.h defines the descriptor format, MAXVA (end of user space),
// USERTOP (end of user memory: below any arch pages at the top) and
// how a user root table is set up (uvminit_root, UVM_ROOT_ENTRIES).

#define PGSHIFT 12
#define PXMASK  0x1FF
#define PXSHIFT(level) (PGSHIFT + (9 * (level)))
#define PX(level, va)  ((((uint64)(va)) >> PXSHIFT(level)) & PXMASK)


// arch-neutral permissions for user mappings; readable is implied.
// The arch turns them into descriptor bits with PTE_USER(perm).
#define PERM_W 0x1
#define PERM_X 0x2

typedef uint64 pte_t;
typedef uint64 *pagetable_t;   // 512 PTEs

pte_t *walk(pagetable_t pagetable, uint64 va, int alloc);
int mappages(pagetable_t pagetable, uint64 va, uint64 size, uint64 pa, uint64 attrs);
void kvminit(void);
void kvmmap(pagetable_t pagetable, uint64 va, uint64 pa, uint64 size, uint64 attrs);
void kvmsync(void);   // arch: new kernel mappings visible to this CPU
void kvmmap_devices(pagetable_t pagetable);   // arch: map device registers

uint64 walkaddr(pagetable_t pagetable, uint64 va);
void uvmunmap(pagetable_t pagetable, uint64 va, uint64 npages, int do_free);
pagetable_t uvmcreate(void);
uint64 uvmalloc(pagetable_t pagetable, uint64 oldsz, uint64 newsz, int perm);
uint64 uvmdealloc(pagetable_t pagetable, uint64 oldsz, uint64 newsz);
void uvmfree(pagetable_t pagetable, uint64 sz);
int uvmcopy(pagetable_t old, pagetable_t new, uint64 sz);
int uvmshare(pagetable_t old, pagetable_t new, uint64 start, uint64 end, int cow);
void uvmclear(pagetable_t pagetable, uint64 va);
uint64 vmfault(pagetable_t pagetable, uint64 va);
uint64 uvmcow(pagetable_t pagetable, uint64 va);
int copyout(pagetable_t pagetable, uint64 dstva, const char *src, uint64 len);
int copyin(pagetable_t pagetable, char *dst, uint64 srcva, uint64 len);
int copyinstr(pagetable_t pagetable, char *dst, uint64 srcva, uint64 max);
void kvminithart(void);   // arch: switch this core to kernel_pagetable
// arch: a new user page table for p, with any pages the arch needs in
// every address space; and its counterpart, freeing user memory too
struct proc;
pagetable_t proc_pagetable(struct proc *p);
void proc_freepagetable(pagetable_t pagetable, uint64 sz);
void kvmswitch(void);                    // arch: back to no user page table
// arch: drop every hart's cached translations tagged with asid, after
// that address space changed or before the ASID is reused
void asid_flush(int asid);
void sync_icache(uint64 va, uint64 len); // arch: make written code executable
void dcache_flush_line(uint64 va);         // arch: memory a device sees = cache

extern pagetable_t kernel_pagetable;

#endif
