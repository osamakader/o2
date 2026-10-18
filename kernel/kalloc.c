// Physical memory allocator, for user processes,
// kernel stacks, page-table pages,
// and pipe buffers. 
// Allocates whole 4096-byte pages.

#include "memlayout.h"
#include "defs.h"
#include "console.h"
#include "spinlock.h"

extern char end[]; // first address after kernel.
                   // defined by kernel.ld.

struct run {
  struct run *next;
};

struct {
  struct spinlock lock;
  struct run *freelist;
  // references to each page of RAM: page tables mapping it (pages
  // shared copy-on-write after fork() have several), or 1 for other
  // uses. Under lock.
  ushort ref[(PHYSTOP - KERNBASE) / PGSIZE];
} kmem;

#define PAGEREF(pa) kmem.ref[(V2P(pa) - KERNBASE) / PGSIZE]


// Drop a reference to the page of physical memory pointed at by pa,
// which normally should have been returned by a call to kalloc(), and
// free it if that was the last one. (The exception is when
// initializing the allocator; see kinit above.)
void kfree(void *pa)
{
  struct run *r;

  if(((uint64)pa % PGSIZE) != 0 || (char*)pa < end || (uint64)pa >= P2V(PHYSTOP))
    panic("kfree");

  acquire(&kmem.lock);
  if(PAGEREF(pa) == 0)
    panic("kfree: free page");
  int last = --PAGEREF(pa) == 0;
  release(&kmem.lock);
  if(!last)
    return;

  // Fill with junk to catch dangling refs.
  memset(pa, 1, PGSIZE);

  r = (struct run*)pa;

  acquire(&kmem.lock);
  r->next = kmem.freelist;
  kmem.freelist = r;
  release(&kmem.lock);
}

void freerange(void *pa_start, void *pa_end)
{
  char *p;
  p = (char*)PGROUNDUP((uint64)pa_start);
  for(; p + PGSIZE <= (char*)pa_end; p += PGSIZE){
    PAGEREF(p) = 1;
    kfree(p);
  }
}

void kinit()
{
  initlock(&kmem.lock, "kmem");
  freerange(end, (void*)P2V(PHYSTOP));
}


// Allocate one 4096-byte page of physical memory.
// Returns its kernel virtual address; V2P() gives the physical one.
// Returns 0 if the memory cannot be allocated.
void * kalloc(void)
{
  struct run *r;

  acquire(&kmem.lock);
  r = kmem.freelist;
  if(r){
    kmem.freelist = r->next;
    PAGEREF(r) = 1;
  }
  release(&kmem.lock);

  if(r)
    memset((char*)r, 5, PGSIZE); // fill with junk
  return (void*)r;
}

// Add a reference to the allocated page pa (another page table maps
// it).
void kref(void *pa)
{
  acquire(&kmem.lock);
  if(PAGEREF(pa) == 0 || PAGEREF(pa) == 0xffff)
    panic("kref");
  PAGEREF(pa)++;
  release(&kmem.lock);
}

// How many references to the allocated page pa?
int krefs(void *pa)
{
  acquire(&kmem.lock);
  int n = PAGEREF(pa);
  release(&kmem.lock);
  return n;
}

// Number of free pages, for leak checks.
uint64 kfreepages(void)
{
  uint64 n = 0;

  acquire(&kmem.lock);
  for(struct run *r = kmem.freelist; r; r = r->next)
    n++;
  release(&kmem.lock);
  return n;
}
