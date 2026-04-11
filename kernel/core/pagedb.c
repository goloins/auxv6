#include "types.h"
#include "defs.h"
#include "param.h"
#include "memlayout.h"
#include "mmu.h"

struct page_descriptor {
  uint pfn;
  uint flags;
  uint refcount;
};

struct page_descriptor *pg_array;
uint pg_count = PHYSTOP / PGSIZE;
static uint pg_bytes;
static uint pg_npages;
static int pg_ready;

static uint
pgfn_to_pa(uint pfn)
{
  return pfn * PGSIZE;
}

static struct page_descriptor*
pgdesc_by_pa(uint pa)
{
  uint pfn;

  if(!pg_ready || pa >= PHYSTOP)
    return 0;
  pfn = pa / PGSIZE;
  if(pfn >= pg_count)
    return 0;
  return &pg_array[pfn];
}

void
pagedb_init(void)
{
  uint i;
  char *base;

  if(pg_ready)
    return;

  pg_bytes = pg_count * sizeof(struct page_descriptor);
  pg_npages = (pg_bytes + PGSIZE - 1) / PGSIZE;

  base = kalloc_contiguous(pg_npages);
  if(base == 0)
    panic("pagedb_init alloc");

  pg_array = (struct page_descriptor*)base;
  for(i = 0; i < pg_count; i++){
    pg_array[i].pfn = i;
    pg_array[i].flags = 0;
    pg_array[i].refcount = 0;
  }

  // Mark descriptor backing pages reserved so they are visible in pagedb state.
  for(i = 0; i < pg_npages; i++){
    uint pfn = V2P(base) / PGSIZE + i;
    if(pfn < pg_count)
      pg_array[pfn].flags |= PAGE_RESERVED;
  }

  // Backfill managed/free classification from existing kpage metadata.
  for(i = 0; i < pg_count; i++){
    uint pa = pgfn_to_pa(i);
    if(kpage_is_managed(pa)){
      pg_array[i].flags |= PAGE_MANAGED;
      pg_array[i].refcount = kpage_refcount(pa);
      if(pg_array[i].refcount == 0)
        pg_array[i].flags |= PAGE_FREE;
    }
  }

  __sync_synchronize();
  pg_ready = 1;
}

void
pagedb_stats(uint *desc_pages, uint *desc_bytes, uint *backing_pages, int *ready)
{
  if(desc_pages)
    *desc_pages = pg_count;
  if(desc_bytes)
    *desc_bytes = pg_bytes;
  if(backing_pages)
    *backing_pages = pg_npages;
  if(ready)
    *ready = pg_ready;
}

int
pagedb_ready(void)
{
  return pg_ready;
}

void
pagedb_flag_counts(uint *managed, uint *freep, uint *reserved, uint *pinned)
{
  uint i;
  uint managed_n;
  uint free_n;
  uint reserved_n;
  uint pinned_n;

  managed_n = 0;
  free_n = 0;
  reserved_n = 0;
  pinned_n = 0;

  if(pg_ready){
    for(i = 0; i < pg_count; i++){
      uint f = pg_array[i].flags;
      if(f & PAGE_MANAGED)
        managed_n++;
      if(f & PAGE_FREE)
        free_n++;
      if(f & PAGE_RESERVED)
        reserved_n++;
      if(f & PAGE_PINNED)
        pinned_n++;
    }
  }

  if(managed)
    *managed = managed_n;
  if(freep)
    *freep = free_n;
  if(reserved)
    *reserved = reserved_n;
  if(pinned)
    *pinned = pinned_n;
}

void
pagedb_mark_managed_pa(uint pa)
{
  struct page_descriptor *d;

  d = pgdesc_by_pa(pa);
  if(d == 0)
    return;
  d->flags |= PAGE_MANAGED;
}

uint
pagedb_refcount_pa(uint pa)
{
  struct page_descriptor *d;

  d = pgdesc_by_pa(pa);
  if(d == 0)
    return 0;
  return d->refcount;
}

void
pagedb_set_refcount_pa(uint pa, uint refs)
{
  struct page_descriptor *d;

  d = pgdesc_by_pa(pa);
  if(d == 0)
    return;
  d->refcount = refs;
}

void
pagedb_mark_free_pa(uint pa)
{
  struct page_descriptor *d;

  d = pgdesc_by_pa(pa);
  if(d == 0)
    return;
  d->flags |= PAGE_FREE;
}

void
pagedb_mark_allocated_pa(uint pa)
{
  struct page_descriptor *d;

  d = pgdesc_by_pa(pa);
  if(d == 0)
    return;
  d->flags &= ~PAGE_FREE;
  d->flags |= PAGE_MANAGED;
}

struct page_descriptor*
pgfn_descriptor(uint pfn)
{
  if(!pg_ready || pfn >= pg_count)
    return 0;

  return &pg_array[pfn];
}

int
pgfn_refcount(uint pfn)
{
  if(pg_ready && pfn < pg_count)
    return (int)pg_array[pfn].refcount;
  if(pfn >= pg_count)
    return 0;
  return (int)kpage_refcount(pgfn_to_pa(pfn));
}

void
pgfn_incref(uint pfn)
{
  if(pfn >= pg_count)
    panic("pgfn_incref");
  kpage_incref(pgfn_to_pa(pfn));
}

void
pgfn_decref(uint pfn)
{
  if(pfn >= pg_count)
    panic("pgfn_decref");
  kfree(P2V(pgfn_to_pa(pfn)));
}

int
pgfn_is_managed(uint pfn)
{
  if(pfn >= pg_count)
    return 0;
  if(pg_ready)
    return (pg_array[pfn].flags & PAGE_MANAGED) != 0;
  return kpage_is_managed(pgfn_to_pa(pfn));
}

uint
pgfn_flags(uint pfn)
{
  if(!pg_ready || pfn >= pg_count)
    return 0;
  return pg_array[pfn].flags;
}
