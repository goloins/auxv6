#include "types.h"
#include "defs.h"
#include "param.h"
#include "spinlock.h"
#include "sleeplock.h"
#include "fs.h"
#include "buf.h"
#include "blockdev.h"

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

  if(ops == 0 || ops->rw == 0)
    return -1;

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

  if(ops->nblocks == 0)
    return 0;

  return ops->nblocks(dev);
}