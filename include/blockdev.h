#ifndef XV6_BLOCKDEV_H
#define XV6_BLOCKDEV_H

#include "types.h"

struct buf;

struct bdevsw {
  // Transfer one cached block buffer to/from backing storage.
  int (*rw)(struct buf *b);
  // Report usable block capacity for this logical device.
  uint (*nblocks)(uint dev);
  const char *name;
};

void bdevinit(void);
int bdev_register(uint dev, const struct bdevsw *ops);
int bdevrw(struct buf *b);
uint bdev_nblocks(uint dev);

#endif