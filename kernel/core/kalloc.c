// Physical memory allocator, intended to allocate
// memory for user processes, kernel stacks, page table pages,
// and pipe buffers. Allocates 4096-byte pages.

#include "types.h"
#include "defs.h"
#include "param.h"
#include "memlayout.h"
#include "mmu.h"
#include "proc.h"
#include "spinlock.h"

void freerange(void *vstart, void *vend);
extern char end[]; // first address after kernel loaded from ELF file
                   // defined by the kernel linker script in kernel.ld

struct run {
  struct run *next;
};

struct {
  struct spinlock lock;
  int use_lock;
  struct run *freelist;
  uint total_pages;
  uint free_pages;
} kmem;

// Initialization happens in two phases.
// 1. main() calls kinit1() while still using entrypgdir to place just
// the pages mapped by entrypgdir on free list.
// 2. main() calls kinit2() with the rest of the physical pages
// after installing a full page table that maps them on all cores.
void
kinit1(void *vstart, void *vend)
{
  initlock(&kmem.lock, "kmem");
  kmem.use_lock = 0;
  freerange(vstart, vend);
}

void
kinit2(void *vstart, void *vend)
{
  freerange(vstart, vend);
  kmem.use_lock = 1;
}

void
freerange(void *vstart, void *vend)
{
  char *p;
  p = (char*)PGROUNDUP((uint)vstart);
  for(; p + PGSIZE <= (char*)vend; p += PGSIZE){
    kmem.total_pages++;
    kfree(p);
  }
}
//PAGEBREAK: 21
// Free the page of physical memory pointed at by v,
// which normally should have been returned by a
// call to kalloc().  (The exception is when
// initializing the allocator; see kinit above.)
void
kfree(char *v)
{
  struct run *r;
  struct cpu *c;
  int flush;
  int i;

  if((uint)v % PGSIZE || v < end || V2P(v) >= PHYSTOP)
    panic("kfree");

#ifdef KDEBUG_KFREE_POISON
  // Fill with junk to catch dangling refs.  Compile with
  // -DKDEBUG_KFREE_POISON to enable at the cost of a full-page
  // write on every free.
  memset(v, 1, PGSIZE);
#endif

  r = (struct run*)v;

  // Early boot: single CPU, no locking, no per-CPU caches.
  if(!kmem.use_lock){
    r->next = kmem.freelist;
    kmem.freelist = r;
    kmem.free_pages++;
    return;
  }

  // Keep interrupts disabled from CPU selection through fast/slow path
  // choice so we cannot migrate and touch two CPU caches in one kfree().
  pushcli();
  c = mycpu();
  if(c->kfree_cache_count < KALLOC_CPU_CACHE){
    c->kfree_cache[c->kfree_cache_count++] = r;
    popcli();
    return;
  }

  acquire(&kmem.lock);
  flush = KALLOC_CPU_CACHE / 2;
  for(i = 0; i < flush && c->kfree_cache_count > 0; i++){
    struct run *p = c->kfree_cache[--c->kfree_cache_count];
    p->next = kmem.freelist;
    kmem.freelist = p;
  }
  c->kfree_cache[c->kfree_cache_count++] = r;
  release(&kmem.lock);
  popcli();
}

// Allocate one 4096-byte page of physical memory.
// Returns a pointer that the kernel can use.
// Returns 0 if the memory cannot be allocated.
char*
kalloc(void)
{
  struct run *r;
  struct run *batch[KALLOC_CPU_CACHE];
  struct cpu *c;
  int n;
  int i;

  // Early boot: single CPU, no locking, no per-CPU caches.
  if(!kmem.use_lock){
    r = kmem.freelist;
    if(r){
      kmem.freelist = r->next;
      if(kmem.free_pages > 0)
        kmem.free_pages--;
    }
    return (char*)r;
  }

  // Keep interrupts disabled from cache check through slow-path lock
  // acquisition so we cannot migrate between CPUs mid-allocation.
  pushcli();
  c = mycpu();
  if(c->kfree_cache_count > 0){
    r = c->kfree_cache[--c->kfree_cache_count];
    popcli();
    return (char*)r;
  }

  acquire(&kmem.lock);
  n = 0;
  while(kmem.freelist && n < KALLOC_CPU_CACHE){
    r = kmem.freelist;
    kmem.freelist = r->next;
    batch[n++] = r;
  }

  // Preserve global freelist pop order despite local LIFO cache.
  for(i = n - 1; i >= 0; i--)
    c->kfree_cache[c->kfree_cache_count++] = batch[i];

  if(c->kfree_cache_count > 0)
    r = c->kfree_cache[--c->kfree_cache_count];
  else
    r = 0;
  release(&kmem.lock);
  popcli();

  return (char*)r;
}

void
kalloc_meminfo(uint *total_pages, uint *free_pages)
{
  uint n;
  struct run *r;
  int i;

  if(kmem.use_lock)
    acquire(&kmem.lock);

  if(total_pages)
    *total_pages = kmem.total_pages;
  if(free_pages){
    n = 0;
    for(r = kmem.freelist; r; r = r->next)
      n++;
    // Approximate: per-CPU counts may change concurrently.
    for(i = 0; i < ncpu; i++)
      n += cpus[i].kfree_cache_count;
    *free_pages = n;
  }

  if(kmem.use_lock)
    release(&kmem.lock);
}

