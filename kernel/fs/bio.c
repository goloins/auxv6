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

void
binit(void)
{
  struct buf *b;

  initlock(&bcache.lock, "bcache");
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
        while(*pp && *pp != b)
          pp = &(*pp)->hash_next;
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

