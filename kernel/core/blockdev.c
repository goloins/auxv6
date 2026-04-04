#include "types.h"
#include "defs.h"
#include "param.h"
#include "spinlock.h"
#include "sleeplock.h"
#include "fs.h"
#include "buf.h"
#include "blockdev.h"
#include "file.h"

static int blockdev_read(struct inode *ip, char *dst, uint off, int n);
static int blockdev_write(struct inode *ip, char *src, uint off, int n);

struct {
  struct spinlock lock;
  struct {
    const struct bdevsw *ops;
    uint parent;
    uint start;
    uint nblocks;
    int is_part;
  } dev[NDEV];
} bdevtable;

void
bdevinit(void)
{
  int i;

  initlock(&bdevtable.lock, "bdev");
  acquire(&bdevtable.lock);
  for(i = 0; i < NDEV; i++){
    bdevtable.dev[i].ops = 0;
    bdevtable.dev[i].parent = 0;
    bdevtable.dev[i].start = 0;
    bdevtable.dev[i].nblocks = 0;
    bdevtable.dev[i].is_part = 0;
  }
  release(&bdevtable.lock);

  devsw[BLOCKDEV].read = blockdev_read;
  devsw[BLOCKDEV].write = blockdev_write;
}

static int
blockdev_read(struct inode *ip, char *dst, uint off, int n)
{
  uint total;
  uint limit;

  if(ip == 0 || dst == 0 || ip->minor < 0 || ip->minor >= NDEV)
    return -1;

  limit = bdev_nblocks(ip->minor) * BSIZE;
  if(off > limit || off + n < off)
    return -1;
  if(off + n > limit)
    n = limit - off;

  for(total = 0; total < (uint)n; ){
    struct buf *bp;
    uint blockno;
    uint blockoff;
    uint chunk;

    blockno = off / BSIZE;
    blockoff = off % BSIZE;
    chunk = BSIZE - blockoff;
    if(chunk > (uint)n - total)
      chunk = (uint)n - total;

    if(bread_ok(ip->minor, blockno, &bp) < 0)
      return -1;
    memmove(dst + total, bp->data + blockoff, chunk);
    brelse(bp);

    total += chunk;
    off += chunk;
  }

  return n;
}

static int
blockdev_write(struct inode *ip, char *src, uint off, int n)
{
  uint total;
  uint limit;

  if(ip == 0 || src == 0 || ip->minor < 0 || ip->minor >= NDEV)
    return -1;

  limit = bdev_nblocks(ip->minor) * BSIZE;
  if(off > limit || off + n < off)
    return -1;
  if(off + n > limit)
    n = limit - off;

  for(total = 0; total < (uint)n; ){
    struct buf *bp;
    uint blockno;
    uint blockoff;
    uint chunk;

    blockno = off / BSIZE;
    blockoff = off % BSIZE;
    chunk = BSIZE - blockoff;
    if(chunk > (uint)n - total)
      chunk = (uint)n - total;

    if(bread_ok(ip->minor, blockno, &bp) < 0)
      return -1;
    memmove(bp->data + blockoff, src + total, chunk);
    if(bwrite_ok(bp) < 0){
      brelse(bp);
      return -1;
    }
    brelse(bp);

    total += chunk;
    off += chunk;
  }

  return n;
}

int
bdev_register(uint dev, const struct bdevsw *ops)
{
  if(dev >= NDEV || ops == 0 || ops->rw == 0)
    return -1;

  acquire(&bdevtable.lock);
  bdevtable.dev[dev].ops = ops;
  bdevtable.dev[dev].parent = dev;
  bdevtable.dev[dev].start = 0;
  bdevtable.dev[dev].nblocks = 0;
  bdevtable.dev[dev].is_part = 0;
  release(&bdevtable.lock);
  return 0;
}

int
bdev_register_part(uint dev, uint parent, uint start, uint nblocks)
{
  const struct bdevsw *ops;

  if(dev >= NDEV || parent >= NDEV || nblocks == 0)
    return -1;

  acquire(&bdevtable.lock);
  ops = bdevtable.dev[parent].ops;
  if(ops == 0){
    release(&bdevtable.lock);
    return -1;
  }
  bdevtable.dev[dev].ops = ops;
  bdevtable.dev[dev].parent = parent;
  bdevtable.dev[dev].start = start;
  bdevtable.dev[dev].nblocks = nblocks;
  bdevtable.dev[dev].is_part = 1;
  release(&bdevtable.lock);
  return 0;
}

int
bdev_set_nblocks(uint dev, uint nblocks)
{
  if(dev >= NDEV || nblocks == 0)
    return -1;

  acquire(&bdevtable.lock);
  if(bdevtable.dev[dev].ops == 0 || bdevtable.dev[dev].is_part){
    release(&bdevtable.lock);
    return -1;
  }
  bdevtable.dev[dev].nblocks = nblocks;
  release(&bdevtable.lock);
  return 0;
}

int
bdevrw(struct buf *b)
{
  const struct bdevsw *ops;
  uint parent;
  uint start;
  uint nblocks;
  int is_part;
  uint orig_dev;
  uint orig_block;
  int r;

  if(b == 0 || b->dev >= NDEV)
    return -1;

  acquire(&bdevtable.lock);
  ops = bdevtable.dev[b->dev].ops;
  parent = bdevtable.dev[b->dev].parent;
  start = bdevtable.dev[b->dev].start;
  nblocks = bdevtable.dev[b->dev].nblocks;
  is_part = bdevtable.dev[b->dev].is_part;
  release(&bdevtable.lock);

  if(ops == 0 || ops->rw == 0){
    cprintf("bdevrw: no ops for dev=%d\n", b->dev);
    return -1;
  }
  if(!is_part)
    return ops->rw(b);

  if(b->blockno >= nblocks)
    return -1;

  orig_dev = b->dev;
  orig_block = b->blockno;
  b->dev = parent;
  b->blockno = orig_block + start;
  r = ops->rw(b);
  b->dev = orig_dev;
  b->blockno = orig_block;

  return r;
}

uint
bdev_nblocks(uint dev)
{
  const struct bdevsw *ops;
  uint nblocks;
  int is_part;

  if(dev >= NDEV)
    return 0;

  acquire(&bdevtable.lock);
  ops = bdevtable.dev[dev].ops;
  nblocks = bdevtable.dev[dev].nblocks;
  is_part = bdevtable.dev[dev].is_part;
  release(&bdevtable.lock);

  if(ops == 0)
    return 0;

  if(is_part)
    return nblocks;

  if(nblocks != 0)
    return nblocks;

  if(ops->nblocks == 0)
    return 0;

  return ops->nblocks(dev);
}

static int
bdev_fmt_str(char *buf, int max, int pos, const char *s)
{
  while(*s && pos < max - 1)
    buf[pos++] = *s++;
  return pos;
}

static int
bdev_fmt_uint(char *buf, int max, int pos, uint v)
{
  char tmp[12];
  int n;

  n = 0;
  if(v == 0){
    tmp[n++] = '0';
  } else {
    while(v){
      tmp[n++] = '0' + (v % 10);
      v /= 10;
    }
  }
  /* tmp[] holds the digits in reverse; write them out forward */
  while(n-- > 0 && pos < max - 1)
    buf[pos++] = tmp[n];
  return pos;
}

/*
 * Format the block device table as human-readable text.
 * One line per registered device (entries with no ops are omitted).
 * Returns number of bytes written (not counting NUL).
 */
int
bdev_format_table(char *buf, int max)
{
  struct {
    uint dev;
    uint nblocks;
    uint parent;
    uint start;
    int  is_part;
    int  has_ops;
    int  has_nblocks_cb;
  } snap[NDEV];
  int i;
  int pos;
  uint query;

  if(buf == 0 || max <= 0)
    return 0;

  acquire(&bdevtable.lock);
  for(i = 0; i < NDEV; i++){
    snap[i].dev            = (uint)i;
    snap[i].has_ops        = (bdevtable.dev[i].ops != 0);
    snap[i].nblocks        = bdevtable.dev[i].nblocks;
    snap[i].is_part        = bdevtable.dev[i].is_part;
    snap[i].parent         = bdevtable.dev[i].parent;
    snap[i].start          = bdevtable.dev[i].start;
    snap[i].has_nblocks_cb = (bdevtable.dev[i].ops != 0 &&
                              bdevtable.dev[i].ops->nblocks != 0);
  }
  release(&bdevtable.lock);

  pos = 0;
  for(i = 0; i < NDEV; i++){
    if(!snap[i].has_ops)
      continue;
    pos = bdev_fmt_str(buf, max, pos, "dev=");
    pos = bdev_fmt_uint(buf, max, pos, snap[i].dev);
    pos = bdev_fmt_str(buf, max, pos, " nblocks=");
    pos = bdev_fmt_uint(buf, max, pos, snap[i].nblocks);
    pos = bdev_fmt_str(buf, max, pos, " is_part=");
    pos = bdev_fmt_uint(buf, max, pos, (uint)snap[i].is_part);
    pos = bdev_fmt_str(buf, max, pos, " parent=");
    pos = bdev_fmt_uint(buf, max, pos, snap[i].parent);
    pos = bdev_fmt_str(buf, max, pos, " start=");
    pos = bdev_fmt_uint(buf, max, pos, snap[i].start);
    pos = bdev_fmt_str(buf, max, pos, " has_nblocks_cb=");
    pos = bdev_fmt_uint(buf, max, pos, (uint)snap[i].has_nblocks_cb);
    /* Effective block count as seen by the rest of the kernel */
    query = bdev_nblocks(snap[i].dev);
    pos = bdev_fmt_str(buf, max, pos, " query=");
    pos = bdev_fmt_uint(buf, max, pos, query);
    if(pos < max - 1)
      buf[pos++] = '\n';
  }
  buf[pos] = 0;
  return pos;
}