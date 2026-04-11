#include "types.h"
#include "defs.h"

static int pagealloc_online;

void
pagealloc_init(void)
{
  // Phase 2 scaffold: wire-on point for per-CPU and zone alloc layers.
  buddy_init();
  pagealloc_online = 1;
}

int
pagealloc_ready(void)
{
  return pagealloc_online;
}

struct page_descriptor*
alloc_single_page(int flags)
{
  return alloc_pages_order(0, flags);
}

void
free_single_page(struct page_descriptor *pg)
{
  free_pages_order(pg, 0);
}
