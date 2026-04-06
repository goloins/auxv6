//
// Simple page-backed kernel allocator.
// kmalloc() returns variable-sized buffers by reserving whole page spans.
// This intentionally favors correctness/stability over memory density.
//

#include "types.h"
#include "defs.h"
#include "mmu.h"

#define KMALLOC_MAGIC 0x4B4D414CU  // "KMAL"

struct kmalloc_hdr {
  uint magic;
  uint npages;
  uint req_size;
};

void
kmalloc_init(void)
{
  // No global allocator state for the page-backed implementation.
}

void*
kmalloc(uint size)
{
  uint total;
  uint npages;
  char *base;
  struct kmalloc_hdr *h;

  if(size == 0)
    return 0;

  total = (uint)sizeof(struct kmalloc_hdr) + size;
  npages = (total + PGSIZE - 1) / PGSIZE;

  if(npages == 1)
    base = kalloc();
  else
    base = kalloc_contiguous(npages);

  if(base == 0)
    return 0;

  h = (struct kmalloc_hdr*)base;
  h->magic = KMALLOC_MAGIC;
  h->npages = npages;
  h->req_size = size;

  return (void*)(h + 1);
}

void
kmalloc_free(void *ptr)
{
  struct kmalloc_hdr *h;
  char *base;
  uint i;
  uint npages;
  uint req_size;

  if(ptr == 0)
    return;

  h = ((struct kmalloc_hdr*)ptr) - 1;
  if(h->magic != KMALLOC_MAGIC){
    // Double-free attempt: magic was already zeroed
    cprintf("kmalloc_free: double-free ptr=%p magic=%x (was KMAL at first free)\n",
            ptr, h->magic);
    return;
  }

  // Snapshot header fields before any kfree(): once base is freed, this
  // header memory may be recycled and must not be consulted again.
  npages = h->npages;
  req_size = h->req_size;

  // Validate header before freeing pages.
  if(npages == 0 || npages > 128){
    cprintf("kmalloc_free: corrupted header ptr=%p npages=%u req_size=%u base=%p\n",
            ptr, npages, req_size, h);
    cprintf("  kfree would iterate [0..%u) at base+i*PGSIZE\n", npages);
    panic("kmalloc_free: bad npages");
  }

  if(req_size == 0 || req_size > (npages * PGSIZE)){
    cprintf("kmalloc_free: suspicious req_size ptr=%p npages=%u req_size=%u\n",
            ptr, npages, req_size);
  }

  base = (char*)h;
  h->magic = 0;

  for(i = 0; i < npages; i++)
    kfree(base + i * PGSIZE);
}

void*
kmalloc_realloc(void *ptr, uint size)
{
  struct kmalloc_hdr *h;
  uint old_size;
  uint copy_n;
  void *newp;

  if(ptr == 0)
    return kmalloc(size);

  if(size == 0){
    kmalloc_free(ptr);
    return 0;
  }

  h = ((struct kmalloc_hdr*)ptr) - 1;
  if(h->magic != KMALLOC_MAGIC)
    panic("kmalloc_realloc: bad header");

  old_size = h->req_size;
  if(old_size >= size)
    return ptr;

  newp = kmalloc(size);
  if(newp == 0)
    return 0;

  copy_n = (old_size < size) ? old_size : size;
  memmove(newp, ptr, copy_n);
  kmalloc_free(ptr);
  return newp;
}
