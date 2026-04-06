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

static int
kalloc_runptr_valid(struct run *r)
{
  if(r == 0)
    return 0;
  if(((uint)r % PGSIZE) != 0)
    return 0;
  if((char*)r < end)
    return 0;
  if(V2P((char*)r) >= PHYSTOP)
    return 0;
  return 1;
}

static void
kalloc_panic_bad_run(const char *where, struct run *r)
{
  cprintf("%s: bad run ptr=%p end=%p phystop=%p\n", where, r, end, P2V(PHYSTOP));
  panic("kalloc freelist corruption");
}

struct kpage_meta {
  uint refcount;
  uint flags;
};

#define KPAGE_MANAGED 0x0001
#define KPAGE_FREE    0x0002

static struct kpage_meta kpage_meta[PHYSTOP / PGSIZE];

typedef char kpage_meta_size_guard[
  (sizeof(kpage_meta) <= KPAGE_META_BYTES_MAX) ? 1 : -1
];

static inline struct kpage_meta*
kpage_meta_pa(uint pa)
{
  if(pa >= PHYSTOP)
    return 0;
  return &kpage_meta[pa / PGSIZE];
}

static void
kpage_mark_managed(uint pa)
{
  struct kpage_meta *meta;

  meta = kpage_meta_pa(pa);
  if(meta == 0)
    panic("kpage_mark_managed");
  meta->flags = KPAGE_MANAGED;
  meta->refcount = 1;
}

static uint
kpage_drop_ref(uint pa)
{
  struct kpage_meta *meta;

  meta = kpage_meta_pa(pa);
  if(meta == 0 || (meta->flags & KPAGE_MANAGED) == 0)
    panic("kpage_drop_ref");
  if(meta->refcount == 0)
    panic("kpage_drop_ref underflow");
  meta->refcount--;
  return meta->refcount;
}

static int
kalloc_free_run_valid(struct run *r)
{
  uint pa;
  struct kpage_meta *meta;

  if(!kalloc_runptr_valid(r))
    return 0;

  pa = V2P((char*)r);
  meta = kpage_meta_pa(pa);
  if(meta == 0)
    return 0;

  // If current pgdir has broken kernel mappings, avoid touching metadata.
  if(!kaddr_writable_current_pgdir((char*)meta))
    return 0;

  if((meta->flags & KPAGE_MANAGED) == 0)
    return 0;
  if((meta->flags & KPAGE_FREE) == 0)
    return 0;
  if(meta->refcount != 0)
    return 0;

  // Guard against stale/corrupt pointers that pass range tests but are
  // not mapped in the current kernel page-table context.
  if(!kaddr_writable_current_pgdir((char*)r))
    return 0;

  return 1;
}

struct {
  struct spinlock lock;
  int use_lock;
  struct run *freelist;
  uint total_pages;
  uint free_pages;
  uint alloc_calls;
  uint free_calls;
  uint cache_alloc_hits;
  uint cache_alloc_misses;
  uint cache_free_inserts;
  uint global_refill_batches;
  uint global_refill_pages;
  uint global_drain_batches;
  uint global_drain_pages;
  uint ref_increments;
  uint deferred_frees;
  uint invalid_cache_drops;
  uint invalid_global_drops;
  uint freelist_head_resets;
  uint duplicate_frees;
} kmem;

static struct run*
kalloc_cache_pop_valid(struct cpu *c)
{
  struct run *r;

  while(c->kfree_cache_count > 0){
    r = c->kfree_cache[--c->kfree_cache_count];
    if(kalloc_free_run_valid(r))
      return r;
    kmem.invalid_cache_drops++;
    if((kmem.invalid_cache_drops & 0x3f) == 1)
      cprintf("kalloc: dropped invalid cache run=%p drops=%u\n",
              r, kmem.invalid_cache_drops);
  }

  return 0;
}

static int
kalloc_pick_refill_goal(int local_count)
{
  int goal;

  goal = KALLOC_REFILL_BATCH;
  if(local_count < KALLOC_PCPU_LOW_WATER){
    int need = KALLOC_PCPU_LOW_WATER - local_count;
    if(goal < need)
      goal = need;
  }
  if(goal < 1)
    goal = 1;
  if(goal > (KALLOC_CPU_CACHE - local_count))
    goal = KALLOC_CPU_CACHE - local_count;
  if(goal < 1)
    goal = 1;
  return goal;
}

static int
kalloc_refill_local(struct cpu *c)
{
  struct run *batch[KALLOC_CPU_CACHE];
  struct run *r;
  int n;
  int i;
  int goal;
  int can_take;

  if(c->kfree_cache_count >= KALLOC_PCPU_LOW_WATER)
    return 0;

  goal = kalloc_pick_refill_goal(c->kfree_cache_count);

  acquire(&kmem.lock);
  can_take = (int)kmem.free_pages - KALLOC_GLOBAL_RESERVE;
  if(can_take < 1)
    can_take = 1;
  if(goal > can_take)
    goal = can_take;

  n = 0;
  while(kmem.freelist && n < goal){
    r = kmem.freelist;
    if(!kalloc_free_run_valid(r)){
      kmem.invalid_global_drops++;
      if((kmem.invalid_global_drops & 0x3f) == 1)
        cprintf("kalloc_refill_local: drop bad global run=%p drops=%u\n",
                r, kmem.invalid_global_drops);

      // Freelist chain is poisoned. Stop trusting next pointers and fall back
      // to controlled OOM behavior instead of chasing potentially arbitrary data.
      kmem.freelist = 0;
      kmem.freelist_head_resets++;
      break;
    }
    if(r->next && !kalloc_runptr_valid(r->next)){
      kmem.invalid_global_drops++;
      kmem.freelist_head_resets++;
      if((kmem.invalid_global_drops & 0x3f) == 1)
        cprintf("kalloc_refill_local: poison next run=%p next=%p drops=%u\n",
                r, r->next, kmem.invalid_global_drops);
      kmem.freelist = 0;
    } else {
      kmem.freelist = r->next;
    }
    batch[n++] = r;
  }

  if(n > 0){
    kmem.global_refill_batches++;
    kmem.global_refill_pages += n;
  }
  release(&kmem.lock);

  // Preserve global freelist pop order despite local LIFO cache.
  for(i = n - 1; i >= 0; i--)
    c->kfree_cache[c->kfree_cache_count++] = batch[i];

  return n;
}

static void
kalloc_drain_local(struct cpu *c)
{
  struct run *p;
  int drain;
  int i;

  if(c->kfree_cache_count < KALLOC_PCPU_HIGH_WATER)
    return;

  drain = c->kfree_cache_count - KALLOC_PCPU_LOW_WATER;
  if(drain > KALLOC_DRAIN_BATCH)
    drain = KALLOC_DRAIN_BATCH;
  if(drain < 1)
    return;

  acquire(&kmem.lock);
  if(kmem.freelist && !kalloc_runptr_valid(kmem.freelist)){
    kmem.freelist = 0;
    kmem.freelist_head_resets++;
  }
  i = 0;
  while(i < drain){
    p = kalloc_cache_pop_valid(c);
    if(p == 0)
      break;
    p->next = kmem.freelist;
    kmem.freelist = p;
    i++;
  }
  kmem.global_drain_batches++;
  kmem.global_drain_pages += i;
  release(&kmem.lock);
}

// Initialization happens in two phases.
// 1. main() calls kinit1() while still using entrypgdir to place just
// the pages mapped by entrypgdir on free list.
// 2. main() calls kinit2() with the rest of the physical pages
// after installing a full page table that maps them on all cores.
void
kinit1(void *vstart, void *vend)
{
  initlock(&kmem.lock, "kmem");
  lockdep_set_rank(&kmem.lock, LOCK_RANK_KMEM, "kmem");
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
    kpage_mark_managed(V2P(p));
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
  uint pa;
  struct kpage_meta *meta;
  uint refs;

  if((uint)v % PGSIZE || v < end || V2P(v) >= PHYSTOP){
    uint pa_probe;

    if((uint)v >= KERNBASE)
      pa_probe = V2P(v);
    else
      pa_probe = 0xffffffffU;
    cprintf("kfree: invalid ptr v=%p pa=%x end=%p phystop=%p caller=%p use_lock=%d\n",
            v, pa_probe, end, P2V(PHYSTOP), __builtin_return_address(0), kmem.use_lock);
    panic("kfree");
  }

  pa = V2P(v);
  meta = kpage_meta_pa(pa);
  if(meta == 0 || !kaddr_writable_current_pgdir((char*)meta) ||
     (meta->flags & KPAGE_MANAGED) == 0)
    panic("kfree unmanaged");

  /*
   * Under stress (heavy fork/COW/unmap churn), a stale duplicate kfree can
   * arrive for a page that's already on the free side (refcount==0, KPAGE_FREE).
   * Ignore it instead of panicking/poisoning freelists via a second enqueue.
   */
  if(meta->refcount == 0){
    if(meta->flags & KPAGE_FREE){
      kmem.duplicate_frees++;
      if((kmem.duplicate_frees & 0xff) == 1)
        cprintf("kfree: duplicate free ignoring pa=%x v=%p caller=%p (total %u)\n",
                pa, v, __builtin_return_address(0), kmem.duplicate_frees);
      return;
    }
    cprintf("kfree: refcount zero without KPAGE_FREE pa=%x flags=%x v=%p\n",
            pa, meta->flags, v);
    panic("kfree refcount state");
  }

  kmem.free_calls++;
  refs = kpage_drop_ref(pa);
  if(refs > 0){
    kmem.deferred_frees++;
    return;
  }
  meta->flags |= KPAGE_FREE;

#ifdef KDEBUG_KFREE_POISON
  // Fill with junk to catch dangling refs.  Compile with
  // -DKDEBUG_KFREE_POISON to enable at the cost of a full-page
  // write on every free.
  memset(v, 1, PGSIZE);
#endif

  r = (struct run*)v;
  if(!kalloc_runptr_valid(r))
    kalloc_panic_bad_run("kfree entry", r);

  // Early boot: single CPU, no locking, no per-CPU caches.
  if(!kmem.use_lock){
    if(kmem.freelist && !kalloc_runptr_valid(kmem.freelist)){
      kmem.freelist = 0;
      kmem.freelist_head_resets++;
    }
    r->next = kmem.freelist;
    kmem.freelist = r;
    kmem.free_pages++;
    return;
  }

  // Keep interrupts disabled from CPU selection through fast/slow path
  // choice so we cannot migrate and touch two CPU caches in one kfree().
  pushcli();
  c = mycpu();
  kalloc_drain_local(c);
  if(c->kfree_cache_count >= KALLOC_CPU_CACHE){
    // Emergency fallback: move one page out under lock and proceed.
    acquire(&kmem.lock);
    if(kmem.freelist && !kalloc_runptr_valid(kmem.freelist)){
      kmem.freelist = 0;
      kmem.freelist_head_resets++;
    }
    if(c->kfree_cache_count > 0){
      struct run *p = kalloc_cache_pop_valid(c);
      if(p){
      p->next = kmem.freelist;
      kmem.freelist = p;
      kmem.global_drain_batches++;
      kmem.global_drain_pages++;
      }
    }
    release(&kmem.lock);
  }
  if(c->kfree_cache_count >= KALLOC_CPU_CACHE)
    panic("kfree local overflow");
  c->kfree_cache[c->kfree_cache_count++] = r;
  kmem.cache_free_inserts++;
  __sync_fetch_and_add(&kmem.free_pages, 1);
  popcli();
}

// Allocate one 4096-byte page of physical memory.
// Returns a pointer that the kernel can use.
// Returns 0 if the memory cannot be allocated.
char*
kalloc(void)
{
  struct run *r;
  struct cpu *c;
  uint pa;
  struct kpage_meta *meta;

  // Early boot: single CPU, no locking, no per-CPU caches.
  kmem.alloc_calls++;
  if(!kmem.use_lock){
    r = kmem.freelist;
    if(r){
      if(!kalloc_free_run_valid(r))
        kalloc_panic_bad_run("kalloc early pop", r);
      kmem.freelist = r->next;
      if(kmem.free_pages > 0)
        kmem.free_pages--;
      pa = V2P((char*)r);
      meta = kpage_meta_pa(pa);
      if(meta == 0 || !kaddr_writable_current_pgdir((char*)meta) ||
         (meta->flags & KPAGE_MANAGED) == 0)
        panic("kalloc early unmanaged");
      meta->flags &= ~KPAGE_FREE;
      meta->refcount = 1;
    }
    return (char*)r;
  }

  // Keep interrupts disabled from cache check through slow-path lock
  // acquisition so we cannot migrate between CPUs mid-allocation.
  pushcli();
  c = mycpu();
  // Phase 2c: preemptive refill only once local cache reaches a lower
  // trigger, reducing eager global-lock traffic under steady load.
  if(c->kfree_cache_count <= KALLOC_PCPU_REFILL_TRIGGER)
    kalloc_refill_local(c);
  r = kalloc_cache_pop_valid(c);
  if(r){
    kmem.cache_alloc_hits++;
    __sync_fetch_and_sub(&kmem.free_pages, 1);
    pa = V2P((char*)r);
    meta = kpage_meta_pa(pa);
    if(meta == 0 || !kaddr_writable_current_pgdir((char*)meta) ||
       (meta->flags & KPAGE_MANAGED) == 0)
      panic("kalloc cache unmanaged");
    meta->flags &= ~KPAGE_FREE;
    meta->refcount = 1;
    popcli();
    return (char*)r;
  }

  kmem.cache_alloc_misses++;

  if(kalloc_refill_local(c) > 0 && c->kfree_cache_count > 0){
    r = kalloc_cache_pop_valid(c);
    if(r == 0){
      popcli();
      return 0;
    }
    __sync_fetch_and_sub(&kmem.free_pages, 1);
    pa = V2P((char*)r);
    meta = kpage_meta_pa(pa);
    if(meta == 0 || !kaddr_writable_current_pgdir((char*)meta) ||
       (meta->flags & KPAGE_MANAGED) == 0)
      panic("kalloc refill unmanaged");
    meta->flags &= ~KPAGE_FREE;
    meta->refcount = 1;
  } else {
    r = 0;
  }
  popcli();

  return (char*)r;
}

char*
kalloc_contiguous(uint npages)
{
  struct run *r;
  struct run *scan_next;
  struct run *prev;
  struct run *run_head;
  struct run *run_prev;
  struct run *run_tail;
  struct run *after;
  struct run *next;
  uint run_len;
  uint last_pa;
  uint pa;
  struct kpage_meta *meta;
  char *base;
  uint marked;
  static uint contiguous_invalid_drops;

  if(npages == 0)
    return 0;

  if(npages == 1)
    return kalloc();

  kmem.alloc_calls++;

  if(!kmem.use_lock){
    return 0;
  }

  acquire(&kmem.lock);
  prev = 0;
  run_head = 0;
  run_prev = 0;
  run_tail = 0;
  run_len = 0;
  last_pa = 0;

  for(r = kmem.freelist; r; prev = r, r = scan_next){
    if(!kalloc_free_run_valid(r)){
      contiguous_invalid_drops++;
      cprintf("kalloc_contiguous: drop bad run=%p drops=%u\n",
              r, contiguous_invalid_drops);
      if(prev)
        prev->next = 0;
      else
        kmem.freelist = 0;
      break;
    }
    scan_next = r->next;
    if(scan_next && !kalloc_runptr_valid(scan_next)){
      contiguous_invalid_drops++;
      cprintf("kalloc_contiguous: drop bad next=%p cur=%p drops=%u\n",
              scan_next, r, contiguous_invalid_drops);
      r->next = 0;
      scan_next = 0;
    }
    pa = V2P((char *)r);
    meta = kpage_meta_pa(pa);
    if(meta && (meta->flags & KPAGE_MANAGED) && (meta->flags & KPAGE_FREE)) {
      if(run_len == 0) {
        run_head = r;
        run_prev = prev;
        run_tail = r;
        run_len = 1;
        last_pa = pa;
      } else if(pa + PGSIZE == last_pa) {
        run_tail = r;
        run_len++;
        last_pa = pa;
      } else {
        run_head = r;
        run_prev = prev;
        run_tail = r;
        run_len = 1;
        last_pa = pa;
      }

      if(run_len == npages)
        break;
    } else {
      run_len = 0;
      run_head = 0;
      run_prev = 0;
      run_tail = 0;
      last_pa = 0;
    }
  }

  if(run_len != npages || run_head == 0 || run_tail == 0){
    release(&kmem.lock);
    return 0;
  }

  after = run_tail->next;
  if(run_prev)
    run_prev->next = after;
  else
    kmem.freelist = after;

  marked = 0;
  for(r = run_head; r != after && marked < npages; r = next, marked++) {
    next = r->next;
    pa = V2P((char *)r);
    meta = kpage_meta_pa(pa);
    if(meta == 0 || (meta->flags & KPAGE_MANAGED) == 0)
      panic("kalloc_contiguous meta");
    meta->flags &= ~KPAGE_FREE;
    meta->refcount = 1;
  }
  if(marked != npages)
    panic("kalloc_contiguous count");

  if(kmem.free_pages >= marked)
    kmem.free_pages -= marked;
  else
    kmem.free_pages = 0;
  release(&kmem.lock);

  base = (char *)run_tail;
  memset(base, 0, npages * PGSIZE);
  return base;
}

void
kalloc_meminfo(uint *total_pages, uint *free_pages)
{
  if(total_pages)
    *total_pages = kmem.total_pages;
  if(free_pages)
    *free_pages = kmem.free_pages;
}

void
kalloc_stats(struct kalloc_stats_k *out)
{
  uint i;
  uint shared_pages;
  uint total_pages;
  uint free_pages;

  if(out == 0)
    return;

  total_pages = kmem.total_pages;
  free_pages = kmem.free_pages;
  shared_pages = 0;
  for(i = 0; i < PHYSTOP / PGSIZE; i++){
    if((kpage_meta[i].flags & KPAGE_MANAGED) == 0)
      continue;
    if(kpage_meta[i].refcount > 1)
      shared_pages++;
  }

  out->total_pages = total_pages;
  out->free_pages = free_pages;
  out->allocated_pages = (total_pages >= free_pages) ? (total_pages - free_pages) : 0;
  out->shared_pages = shared_pages;
  out->alloc_calls = kmem.alloc_calls;
  out->free_calls = kmem.free_calls;
  out->cache_alloc_hits = kmem.cache_alloc_hits;
  out->cache_alloc_misses = kmem.cache_alloc_misses;
  out->cache_free_inserts = kmem.cache_free_inserts;
  out->global_refill_batches = kmem.global_refill_batches;
  out->global_refill_pages = kmem.global_refill_pages;
  out->global_drain_batches = kmem.global_drain_batches;
  out->global_drain_pages = kmem.global_drain_pages;
  out->ref_increments = kmem.ref_increments;
  out->deferred_frees = kmem.deferred_frees;
}

void
kpage_incref(uint pa)
{
  struct kpage_meta *meta;

  meta = kpage_meta_pa(pa);
  if(meta == 0 || (meta->flags & KPAGE_MANAGED) == 0)
    panic("kpage_incref");
  if(meta->refcount == 0)
    panic("kpage_incref free");
  __sync_fetch_and_add(&meta->refcount, 1);
  meta->flags &= ~KPAGE_FREE;
  __sync_fetch_and_add(&kmem.ref_increments, 1);
}

uint
kpage_refcount(uint pa)
{
  struct kpage_meta *meta;

  meta = kpage_meta_pa(pa);
  if(meta == 0 || (meta->flags & KPAGE_MANAGED) == 0)
    return 0;
  return meta->refcount;
}

int
kpage_is_managed(uint pa)
{
  struct kpage_meta *meta;

  meta = kpage_meta_pa(pa);
  if(meta == 0)
    return 0;
  return (meta->flags & KPAGE_MANAGED) != 0;
}

