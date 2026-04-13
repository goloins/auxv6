// Buffer cache.
//
// The buffer cache is a linked list of buf structures holding
// cached copies of disk block contents.  Caching disk blocks
// in memory reduces the number of disk reads and also provides
// a synchronization point for disk blocks used by multiple processes.
//
// Interface:
// * To get a buffer for a particular disk block, call bread.
// * After changing buffer data, call bwrite to write it to disk.
// * When done with the buffer, call brelse.
// * Do not use the buffer after calling brelse.
// * Only one process at a time can use a buffer,
//     so do not keep them longer than necessary.
//
// The implementation uses two state flags internally:
// * B_VALID: the buffer data has been read from the disk.
// * B_DIRTY: the buffer data has been modified
//     and needs to be written to disk.

#include "types.h"
#include "defs.h"
#include "param.h"
#include "spinlock.h"
#include "sleeplock.h"
#include "fs.h"
#include "buf.h"
#include "blockdev.h"

// Hash table size for the buffer cache.  A power of two roughly equal to
// NBUF/2 keeps average chain length near 2 even after the cache fills.
// Lookup is O(chain) instead of O(NBUF) which was the original design.
#define BCACHE_HASH_SIZE 64
#define BHASH(dev, blockno) (((uint)(dev) * 31u + (uint)(blockno)) & (BCACHE_HASH_SIZE - 1))

struct {
  struct spinlock lock;
  struct buf buf[NBUF];

  // Linked list of all buffers, through prev/next.
  // head.next is most recently used.
  struct buf head;

  // Hash chains for O(1) block lookup.  The hash is indexed by
  // BHASH(dev, blockno).  Buffers stay in the hash until they are
  // evicted (given a new dev/blockno assignment).
  struct buf *hash[BCACHE_HASH_SIZE];
} bcache;

static int
bcache_bufptr_valid(struct buf *b)
{
  uint off;

  if(b == 0)
    return 0;
  if(b < bcache.buf || b >= bcache.buf + NBUF)
    return 0;

  off = (uint)(b - bcache.buf);
  if(off >= NBUF)
    return 0;
  return 1;
}

void
binit(void)
{
  struct buf *b;

  initlock(&bcache.lock, "bcache");
  lockdep_set_rank(&bcache.lock, LOCK_RANK_DEFAULT, "bcache");
  memset(bcache.hash, 0, sizeof(bcache.hash));

//PAGEBREAK!
  // Create linked list of buffers
  bcache.head.prev = &bcache.head;
  bcache.head.next = &bcache.head;
  for(b = bcache.buf; b < bcache.buf+NBUF; b++){
    b->next = bcache.head.next;
    b->prev = &bcache.head;
    b->hash_next = 0;
    initsleeplock(&b->lock, "buffer");
    bcache.head.next->prev = b;
    bcache.head.next = b;
  }
}

// Look through buffer cache for block on device dev.
// If not found, allocate a buffer.
// In either case, return locked buffer.
static struct buf*
bget(uint dev, uint blockno)
{
  struct buf *b, **pp;
  uint h;

  acquire(&bcache.lock);

  // Is the block already cached?  O(1) hash lookup instead of O(NBUF).
  h = BHASH(dev, blockno);
  for(b = bcache.hash[h]; b != 0; b = b->hash_next){
    if(!bcache_bufptr_valid(b)){
      cprintf("bget: corrupt hash[%d]=%p (dev=%d block=%d)\n", h, b, dev, blockno);
      bcache.hash[h] = 0;
      break;
    }
    if(b->dev == dev && b->blockno == blockno){
      b->refcnt++;
      release(&bcache.lock);
      acquiresleep(&b->lock);
      return b;
    }
  }

  // Not cached; recycle the least-recently-used unused buffer.
  // Even if refcnt==0, B_DIRTY indicates a buffer is in use
  // because log.c has modified it but not yet committed it.
  for(b = bcache.head.prev; b != &bcache.head; b = b->prev){
    if(b->refcnt == 0 && (b->flags & B_DIRTY) == 0) {
      // Remove buffer from its old hash chain before reassigning.
      if(b->flags & B_INHASH){
        uint old_h = BHASH(b->dev, b->blockno);
        pp = &bcache.hash[old_h];
        while(*pp && *pp != b){
          if(!bcache_bufptr_valid(*pp)){
            cprintf("bget: corrupt chain hash[%d]=%p while evicting %p\n", old_h, *pp, b);
            bcache.hash[old_h] = 0;
            pp = &bcache.hash[old_h];
            break;
          }
          pp = &(*pp)->hash_next;
        }
        if(*pp)
          *pp = b->hash_next;
      }
      // Assign new identity; insert into new hash chain.
      b->dev = dev;
      b->blockno = blockno;
      b->flags = B_INHASH;   // clear B_VALID/B_DIRTY/B_ERROR, keep in-hash
      b->refcnt = 1;
      b->hash_next = bcache.hash[h];
      bcache.hash[h] = b;
      release(&bcache.lock);
      acquiresleep(&b->lock);
      return b;
    }
  }
  panic("bget: no buffers");
}

// Return a locked buf with the contents of the indicated block.
// Return a locked buf with the contents of the indicated block.
struct buf*
bread(uint dev, uint blockno)
{
  struct buf *b;

  b = bget(dev, blockno);
  if((b->flags & B_VALID) == 0) {
    if(bdevrw(b) < 0){
      cprintf("bread: failed dev=%d blockno=%d\n", dev, blockno);
      b->flags |= (B_ERROR | B_VALID);
      memset(b->data, 0, BSIZE);
      return b;
    }
    b->flags &= ~B_ERROR;
  }
  return b;
}

int
berror(struct buf *b)
{
  if(b == 0)
    return 1;
  return (b->flags & B_ERROR) != 0;
}

int
bread_ok(uint dev, uint blockno, struct buf **out)
{
  struct buf *b;

  if(out == 0)
    return -1;
  *out = 0;

  b = bread(dev, blockno);
  if(b == 0)
    return -1;
  if(berror(b)){
    brelse(b);
    return -1;
  }

  *out = b;
  return 0;
}

int
bwrite_ok(struct buf *b)
{
  if(b == 0)
    return -1;
  bwrite(b);
  if(berror(b))
    return -1;
  return 0;
}

// Write b's contents to disk.  Must be locked.
void
bwrite(struct buf *b)
{
  if(!holdingsleep(&b->lock))
    panic("bwrite");
  b->flags |= B_DIRTY;
  if(bdevrw(b) < 0){
    b->flags |= B_ERROR;
    cprintf("bwrite: failed dev=%d blockno=%d\n", b->dev, b->blockno);
    return;
  }
  b->flags &= ~B_ERROR;
}

// Walk the entire bcache under lock and audit structural invariants.
// Writes a NUL-terminated human-readable report into buf[max].
// Returns number of bytes written (not counting NUL), or -1 on overflow.
// Safe to call from any context that can acquire bcache.lock.
int
bcache_health_check(char *out, int max)
{
  int pos = 0;
  uint i;

  // --- helpers -----------------------------------------------------------
#define _put_str(s) do { \
    const char *_p = (s); \
    while(*_p && pos < max - 1) out[pos++] = *_p++; \
  } while(0)
#define _put_uint(v) do { \
    uint _v = (v); \
    char _tmp[12]; int _n = 0; \
    if(_v == 0) { _tmp[_n++] = '0'; } \
    else { while(_v){ _tmp[_n++] = '0' + (_v % 10); _v /= 10; } } \
    for(int _i = _n-1; _i >= 0; _i--) { if(pos < max-1) out[pos++] = _tmp[_i]; } \
  } while(0)
#define _kv_u(k, v) do { _put_str(k); _put_uint(v); if(pos < max-1) out[pos++] = '\n'; } while(0)

  // tallies
  uint hash_entries    = 0;
  uint hash_corrupt    = 0;    // bad pointer in hash chains
  uint hash_cycles     = 0;    // chain longer than NBUF (cycle detection)
  uint lru_entries     = 0;
  uint lru_corrupt     = 0;    // bad pointer in LRU list
  uint lru_cycles      = 0;    // cycle in LRU list
  uint refcnt_nonzero  = 0;    // bufs currently pinned
  uint dirty_count     = 0;
  uint valid_count     = 0;
  uint inhash_count    = 0;
  uint error_count     = 0;
  uint double_hash     = 0;    // same buf appears in >1 hash bucket

  // per-buf seen bitmap (1 bit per buf, NBUF <= 200 so 32 bytes)
  uint seen[(NBUF + 31) / 32];
  memset(seen, 0, sizeof(seen));

  acquire(&bcache.lock);

  // --- audit hash chains -------------------------------------------------
  for(i = 0; i < BCACHE_HASH_SIZE; i++){
    struct buf *b = bcache.hash[i];
    uint depth = 0;

    while(b != 0){
      if(!bcache_bufptr_valid(b)){
        hash_corrupt++;
        break;
      }
      uint idx = (uint)(b - bcache.buf);
      if(seen[idx >> 5] & (1u << (idx & 31))){
        double_hash++;    // already counted from another chain / earlier in this one
      } else {
        seen[idx >> 5] |= (1u << (idx & 31));
        hash_entries++;
        if(b->flags & B_DIRTY)   dirty_count++;
        if(b->flags & B_VALID)   valid_count++;
        if(b->flags & B_INHASH)  inhash_count++;
        if(b->flags & B_ERROR)   error_count++;
        if(b->refcnt > 0)        refcnt_nonzero++;
      }
      b = b->hash_next;
      if(++depth > NBUF){ hash_cycles++; break; }
    }
  }

  // --- audit LRU list ----------------------------------------------------
  memset(seen, 0, sizeof(seen));
  {
    struct buf *b;
    uint depth = 0;

    for(b = bcache.head.next; b != &bcache.head; b = b->next){
      if(!bcache_bufptr_valid(b)){
        lru_corrupt++;
        break;
      }
      uint idx = (uint)(b - bcache.buf);
      if(seen[idx >> 5] & (1u << (idx & 31))){
        lru_cycles++;
        break;
      }
      seen[idx >> 5] |= (1u << (idx & 31));
      lru_entries++;
      if(++depth > NBUF){ lru_cycles++; break; }
    }
  }

  release(&bcache.lock);

  // --- format output -----------------------------------------------------
  _kv_u("bcache_hash_entries ",   hash_entries);
  _kv_u("bcache_hash_corrupt ",   hash_corrupt);
  _kv_u("bcache_hash_cycles ",    hash_cycles);
  _kv_u("bcache_double_hash ",    double_hash);
  _kv_u("bcache_lru_entries ",    lru_entries);
  _kv_u("bcache_lru_corrupt ",    lru_corrupt);
  _kv_u("bcache_lru_cycles ",     lru_cycles);
  _kv_u("bcache_refcnt_nonzero ", refcnt_nonzero);
  _kv_u("bcache_dirty ",          dirty_count);
  _kv_u("bcache_valid ",          valid_count);
  _kv_u("bcache_inhash ",         inhash_count);
  _kv_u("bcache_error_bufs ",     error_count);
  _kv_u("bcache_nbuf ",           (uint)NBUF);

#undef _put_str
#undef _put_uint
#undef _kv_u

  if(pos >= max)
    return -1;
  out[pos] = 0;
  return pos;
}

// Release a locked buffer.
// Move to the head of the MRU list.
void
brelse(struct buf *b)
{
  if(!holdingsleep(&b->lock))
    panic("brelse");

  releasesleep(&b->lock);

  acquire(&bcache.lock);
  b->refcnt--;
  if (b->refcnt == 0) {
    // no one is waiting for it.
    b->next->prev = b->prev;
    b->prev->next = b->next;
    b->next = bcache.head.next;
    b->prev = &bcache.head;
    bcache.head.next->prev = b;
    bcache.head.next = b;
  }
  
  release(&bcache.lock);
}
//PAGEBREAK!
// Blank page.

