#include "types.h"
#include "defs.h"
#include "memlayout.h"
#include "mmu.h"

#define BUDDY_BRIDGE_MAX_ORDER 7

static uint buddy_alloc_order[BUDDY_BRIDGE_MAX_ORDER + 1];
static uint buddy_free_order[BUDDY_BRIDGE_MAX_ORDER + 1];
static uint buddy_alloc_fail_order[BUDDY_BRIDGE_MAX_ORDER + 1];
static uint buddy_bad_order_requests;
static uint buddy_free_estimate_order[BUDDY_BRIDGE_MAX_ORDER + 1];
static uint buddy_free_invalid_order;
static uint buddy_free_invalid_desc;
static uint buddy_free_double_free;

static int
buddy_block_all_free(uint start_pfn, uint order)
{
  uint i;
  uint n;

  n = 1U << order;
  if(start_pfn + n > pg_count)
    return 0;

  for(i = 0; i < n; i++){
    uint flags = pgfn_flags(start_pfn + i);
    if((flags & PAGE_MANAGED) == 0)
      return 0;
    if((flags & PAGE_FREE) == 0)
      return 0;
    if(flags & PAGE_RESERVED)
      return 0;
  }
  return 1;
}

static void
buddy_refresh_free_estimates(void)
{
  uint pfn;
  int order;
  uint i;

  for(i = 0; i <= BUDDY_BRIDGE_MAX_ORDER; i++)
    buddy_free_estimate_order[i] = 0;

  for(pfn = 0; pfn < pg_count; ){
    uint flags = pgfn_flags(pfn);
    if((flags & PAGE_MANAGED) == 0 || (flags & PAGE_FREE) == 0 || (flags & PAGE_RESERVED) != 0){
      pfn++;
      continue;
    }

    order = BUDDY_BRIDGE_MAX_ORDER;
    while(order > 0){
      uint block = 1U << order;
      if((pfn & (block - 1U)) != 0){
        order--;
        continue;
      }
      if(buddy_block_all_free(pfn, (uint)order))
        break;
      order--;
    }

    buddy_free_estimate_order[order]++;
    pfn += 1U << order;
  }
}

void
buddy_init(void)
{
  // Phase 2 scaffold: real buddy structures land in follow-up slices.
  buddy_refresh_free_estimates();
}

struct page_descriptor*
alloc_pages_order(int order, int flags)
{
  char *v;
  uint pfn;
  uint npages;

  if(order < 0 || order > BUDDY_BRIDGE_MAX_ORDER){
    buddy_bad_order_requests++;
    return 0;
  }

  (void)flags;
  npages = 1U << order;
  if(order == 0)
    v = kalloc_legacy_page();
  else
    v = kalloc_contiguous(npages);
  if(v == 0)
  {
    buddy_alloc_fail_order[order]++;
    return 0;
  }

  pfn = V2P(v) / PGSIZE;
  if(pgfn_descriptor(pfn) == 0){
    buddy_alloc_fail_order[order]++;
    return 0;
  }
  buddy_alloc_order[order]++;
  return pgfn_descriptor(pfn);
}

void
free_pages_order(struct page_descriptor *pg, int order)
{
  uint pfn;
  uint i;
  uint npages;

  if(order < 0 || order > BUDDY_BRIDGE_MAX_ORDER){
    buddy_bad_order_requests++;
    buddy_free_invalid_order++;
    return;
  }
  if(pg == 0)
    return;
  if(pgfn_from_descriptor(pg, &pfn) < 0){
    buddy_free_invalid_desc++;
    return;
  }

  npages = 1U << order;
  if(pfn + npages > pg_count){
    buddy_free_invalid_desc++;
    return;
  }

  for(i = 0; i < npages; i++){
    uint flags = pgfn_flags(pfn + i);
    if(flags & PAGE_FREE){
      buddy_free_double_free++;
      return;
    }
  }

  for(i = 0; i < npages; i++)
    kfree(P2V((pfn + i) * PGSIZE));

  buddy_free_order[order]++;
}

void
buddy_stats(uint *alloc_order0, uint *free_order0, uint *bad_order_requests)
{
  if(alloc_order0)
    *alloc_order0 = buddy_alloc_order[0];
  if(free_order0)
    *free_order0 = buddy_free_order[0];
  if(bad_order_requests)
    *bad_order_requests = buddy_bad_order_requests;
}

void
buddy_stats_order(uint order, uint *allocs, uint *frees)
{
  buddy_refresh_free_estimates();

  if(order > BUDDY_BRIDGE_MAX_ORDER){
    if(allocs)
      *allocs = 0;
    if(frees)
      *frees = 0;
    return;
  }
  if(allocs)
    *allocs = buddy_alloc_order[order];
  if(frees)
    *frees = buddy_free_order[order];
}

uint
buddy_free_estimate(uint order)
{
  buddy_refresh_free_estimates();
  if(order > BUDDY_BRIDGE_MAX_ORDER)
    return 0;
  return buddy_free_estimate_order[order];
}

void
buddy_stats_all(uint *allocs8, uint *frees8, uint *est_free8,
                uint *bad_order_requests)
{
  uint i;

  buddy_refresh_free_estimates();

  for(i = 0; i <= BUDDY_BRIDGE_MAX_ORDER; i++){
    if(allocs8)
      allocs8[i] = buddy_alloc_order[i];
    if(frees8)
      frees8[i] = buddy_free_order[i];
    if(est_free8)
      est_free8[i] = buddy_free_estimate_order[i];
  }
  if(bad_order_requests)
    *bad_order_requests = buddy_bad_order_requests;
}

void
buddy_error_stats(uint *alloc_fail8,
                  uint *free_invalid_order,
                  uint *free_invalid_desc,
                  uint *free_double_free)
{
  uint i;

  if(alloc_fail8){
    for(i = 0; i <= BUDDY_BRIDGE_MAX_ORDER; i++)
      alloc_fail8[i] = buddy_alloc_fail_order[i];
  }
  if(free_invalid_order)
    *free_invalid_order = buddy_free_invalid_order;
  if(free_invalid_desc)
    *free_invalid_desc = buddy_free_invalid_desc;
  if(free_double_free)
    *free_double_free = buddy_free_double_free;
}

void
buddy_invariant_stats(uint *ok, uint *bad_free_not_managed,
                      uint *bad_free_refcount_nonzero,
                      uint *bad_reserved_free)
{
  uint pfn;
  uint n_bad_free_not_managed;
  uint n_bad_free_refcount_nonzero;
  uint n_bad_reserved_free;

  n_bad_free_not_managed = 0;
  n_bad_free_refcount_nonzero = 0;
  n_bad_reserved_free = 0;

  for(pfn = 0; pfn < pg_count; pfn++){
    uint flags = pgfn_flags(pfn);
    if(flags & PAGE_FREE){
      if((flags & PAGE_MANAGED) == 0)
        n_bad_free_not_managed++;
      if(pgfn_refcount(pfn) != 0)
        n_bad_free_refcount_nonzero++;
      if(flags & PAGE_RESERVED)
        n_bad_reserved_free++;
    }
  }

  if(ok)
    *ok = (n_bad_free_not_managed == 0 &&
           n_bad_free_refcount_nonzero == 0 &&
           n_bad_reserved_free == 0) ? 1U : 0U;
  if(bad_free_not_managed)
    *bad_free_not_managed = n_bad_free_not_managed;
  if(bad_free_refcount_nonzero)
    *bad_free_refcount_nonzero = n_bad_free_refcount_nonzero;
  if(bad_reserved_free)
    *bad_reserved_free = n_bad_reserved_free;
}
