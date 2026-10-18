// Physical memory allocator, for user processes,
// kernel stacks, page-table pages,
// and pipe buffers.
// Allocates whole 4096-byte pages.
//
// Each CPU has its own free list and lock, so CPUs allocating at the
// same time don't contend; a CPU whose list is empty takes a batch of
// pages from another's. Reference counts are updated atomically.

#include "param.h"
#include "memlayout.h"
#include "defs.h"
#include "console.h"
#include "spinlock.h"
#include "board.h"
#include "cpu.h"

extern char end[]; // first address after kernel.
                   // defined by kernel.ld.

struct run {
  struct run *next;
};

struct {
  struct spinlock lock;
  struct run *freelist;
} kmem[NCPU];

// References to each page of RAM: page tables mapping it (pages shared
// copy-on-write after fork() have several), or 1 for other uses.
// Atomic. In the first pages after the kernel.
static ushort *ref;

#define PAGEREF(pa) ref[(V2P(pa) - KERNBASE) / PGSIZE]

// pages a CPU takes from another's list at a time
#define STEAL 64

// Drop a reference to the page of physical memory pointed at by pa,
// which normally should have been returned by a call to kalloc(), and
// free it if that was the last one. (The exception is when
// initializing the allocator; see kinit above.)
void kfree(void *pa)
{
  struct run *r;

  if(((uint64)pa % PGSIZE) != 0 || (char*)pa < end || (uint64)pa >= P2V(board.ram_end))
    panic("kfree");

  int old = __atomic_fetch_sub(&PAGEREF(pa), 1, __ATOMIC_ACQ_REL);
  if(old == 0)
    panic("kfree: free page");
  if(old > 1)
    return;

  // Fill with junk to catch dangling refs.
  memset(pa, 1, PGSIZE);

  r = (struct run*)pa;

  push_off();
  int c = cpuid();
  acquire(&kmem[c].lock);
  r->next = kmem[c].freelist;
  kmem[c].freelist = r;
  release(&kmem[c].lock);
  pop_off();
}

// Give the pages in [pa_start, pa_end) (kernel addresses) to the
// allocator, but not reserved ones (board_reserved()).
void freerange(void *pa_start, void *pa_end)
{
  char *p;
  p = (char*)PGROUNDUP((uint64)pa_start);
  for(; p + PGSIZE <= (char*)pa_end; p += PGSIZE){
    if(board_reserved(V2P(p), V2P(p) + PGSIZE))
      continue;
    PAGEREF(p) = 1;
    kfree(p);
  }
}

// RAM up to here has been given to the allocator.
static uint64 freed;

// Give the RAM after the kernel that the boot page table maps to the
// allocator (to this CPU's list; the others take from it), but for the
// reference counts, which go first.
void kinit()
{
  for(int i = 0; i < NCPU; i++)
    initlock(&kmem[i].lock, "kmem");
  uint64 npages = (board.ram_end - KERNBASE) / PGSIZE;
  ref = (ushort*)PGROUNDUP((uint64)end);
  memset(ref, 0, npages * sizeof(ushort));
  freed = board.ram_end < KERNBASE + BOOT_RAM ? board.ram_end : KERNBASE + BOOT_RAM;
  freerange(ref + npages, (void*)P2V(freed));
}

// The rest of RAM, once the kernel page table maps it.
void kinit_rest()
{
  freerange((void*)P2V(freed), (void*)P2V(board.ram_end));
  freed = board.ram_end;
}

// Take up to STEAL pages from another CPU's list; returns them as a
// list, or 0. Holds one lock at a time: two CPUs stealing from each
// other can't deadlock.
static struct run *steal(int c)
{
  for(int i = 1; i < NCPU; i++){
    int v = (c + i) % NCPU;
    acquire(&kmem[v].lock);
    struct run *first = kmem[v].freelist, *last = first;
    if(first){
      for(int n = 1; n < STEAL && last->next; n++)
        last = last->next;
      kmem[v].freelist = last->next;
      last->next = 0;
    }
    release(&kmem[v].lock);
    if(first)
      return first;
  }
  return 0;
}

// Allocate one 4096-byte page of physical memory.
// Returns its kernel virtual address; V2P() gives the physical one.
// Returns 0 if the memory cannot be allocated.
void * kalloc(void)
{
  struct run *r;

  push_off();
  int c = cpuid();
  acquire(&kmem[c].lock);
  r = kmem[c].freelist;
  if(r)
    kmem[c].freelist = r->next;
  release(&kmem[c].lock);

  if(r == 0 && (r = steal(c)) != 0){
    // keep the rest of the batch
    acquire(&kmem[c].lock);
    struct run *last = r;
    while(last->next)
      last = last->next;
    last->next = kmem[c].freelist;
    kmem[c].freelist = r->next;
    release(&kmem[c].lock);
  }
  pop_off();

  if(r){
    __atomic_store_n(&PAGEREF(r), 1, __ATOMIC_RELEASE);
    memset((char*)r, 5, PGSIZE); // fill with junk
  }
  return (void*)r;
}

// Add a reference to the allocated page pa (another page table maps
// it).
void kref(void *pa)
{
  int old = __atomic_fetch_add(&PAGEREF(pa), 1, __ATOMIC_ACQ_REL);
  if(old == 0 || old == 0xffff)
    panic("kref");
}

// How many references to the allocated page pa?
int krefs(void *pa)
{
  return __atomic_load_n(&PAGEREF(pa), __ATOMIC_ACQUIRE);
}

// Number of free pages, for leak checks.
uint64 kfreepages(void)
{
  uint64 n = 0;

  for(int i = 0; i < NCPU; i++){
    acquire(&kmem[i].lock);
    for(struct run *r = kmem[i].freelist; r; r = r->next)
      n++;
    release(&kmem[i].lock);
  }
  return n;
}
