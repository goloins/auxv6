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
  const struct bdevsw *dev[NDEV];
} bdevtable;

void
bdevinit(void)
{
  int i;

  initlock(&bdevtable.lock, "bdev");
  acquire(&bdevtable.lock);
  for(i = 0; i < NDEV; i++)
    bdevtable.dev[i] = 0;
  release(&bdevtable.lock);
}

int
bdev_register(uint dev, const struct bdevsw *ops)
{
  if(dev >= NDEV || ops == 0 || ops->rw == 0)
    return -1;

  acquire(&bdevtable.lock);
  bdevtable.dev[dev] = ops;
  release(&bdevtable.lock);
  return 0;
}

int
bdevrw(struct buf *b)
{
  const struct bdevsw *ops;

  if(b == 0 || b->dev >= NDEV)
    return -1;

  acquire(&bdevtable.lock);
  ops = bdevtable.dev[b->dev];
  release(&bdevtable.lock);

  if(ops == 0 || ops->rw == 0)
    return -1;

  return ops->rw(b);
}

uint
bdev_nblocks(uint dev)
{
  const struct bdevsw *ops;

  if(dev >= NDEV)
    return 0;

  acquire(&bdevtable.lock);
  ops = bdevtable.dev[dev];
  release(&bdevtable.lock);

  if(ops == 0 || ops->nblocks == 0)
    return 0;

  return ops->nblocks(dev);
}