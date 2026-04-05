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

  if(ptr == 0)
    return;

  h = ((struct kmalloc_hdr*)ptr) - 1;
  if(h->magic != KMALLOC_MAGIC)
    panic("kmalloc_free: bad header");

  base = (char*)h;
  h->magic = 0;

  for(i = 0; i < h->npages; i++)
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
