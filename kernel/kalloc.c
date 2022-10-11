// Physical memory allocator, for user processes,
// kernel stacks, page-table pages,
// and pipe buffers. Allocates whole 4096-byte pages.

#include "types.h"
#include "param.h"
#include "memlayout.h"
#include "spinlock.h"
#include "riscv.h"
#include "defs.h"

void freerange(void *pa_start, void *pa_end);

extern char end[]; // first address after kernel.
                   // defined by kernel.ld.

struct run {
  struct run *next;
};

struct {
  struct spinlock lock;
  struct run *freelist;
} kmem;

int pgrc[(PGROUNDUP(PHYSTOP) - KERNBASE) / PGSIZE] = {0};

void
incpgrc(void * pa)
{
  int index = (PGROUNDDOWN((uint64) pa) - KERNBASE) / PGSIZE;
  if(pgrc[index] < 0)
  {
    panic("negative ref count in incpgrc\n");
  }
  pgrc[index]++;
}

void
decpgrc(void * pa)
{
  int index = (PGROUNDDOWN((uint64) pa) - KERNBASE) / PGSIZE;
  if(pgrc[index] <= 0)
  {
    panic("negative ref count in decpgrc\n");
  }
  pgrc[index]--;
}

void
kinit()
{
  initlock(&kmem.lock, "kmem");
  freerange(end, (void*)PHYSTOP);
}

void
freerange(void *pa_start, void *pa_end)
{
  char *p;
  p = (char*)PGROUNDUP((uint64)pa_start);
  for(; p + PGSIZE <= (char*)pa_end; p += PGSIZE)
  {
    incpgrc(p);
    kfree(p);
  }
}

// Free the page of physical memory pointed at by pa,
// which normally should have been returned by a
// call to kalloc().  (The exception is when
// initializing the allocator; see kinit above.)
void
kfree(void *pa)
{
  struct run *r;

  if(((uint64)pa % PGSIZE) != 0 || (char*)pa < end || (uint64)pa >= PHYSTOP)
    panic("kfree");

  decpgrc(pa);

  int index = (PGROUNDDOWN((uint64) pa) - KERNBASE) / PGSIZE;
  if(pgrc[index] == 0)
  {
    // Fill with junk to catch dangling refs.
    memset(pa, 1, PGSIZE);

    r = (struct run*)pa;

    acquire(&kmem.lock);
    r->next = kmem.freelist;
    kmem.freelist = r;
    release(&kmem.lock);
  }
}

// Allocate one 4096-byte page of physical memory.
// Returns a pointer that the kernel can use.
// Returns 0 if the memory cannot be allocated.
void *
kalloc(void)
{
  struct run *r;

  acquire(&kmem.lock);
  r = kmem.freelist;
  if(r)
    kmem.freelist = r->next;
  release(&kmem.lock);

  if(r)
  {
    memset((char*)r, 5, PGSIZE); // fill with junk
    incpgrc(r);
  }
  return (void*)r;
}
