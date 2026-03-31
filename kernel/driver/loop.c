/*
 * Loop block device driver
 *
 * Provides /dev/loop0 through /dev/loop7 as block devices backed by
 * regular files or other block devices. This allows mounting ISO images
 * and disk images without real hardware.
 */

#include "types.h"
#include "defs.h"
#include "param.h"
#include "spinlock.h"
#include "sleeplock.h"
#include "fs.h"
#include "buf.h"
#include "blockdev.h"
#include "file.h"
#include "stat.h"
#include "vfs.h"

#define NLOOP 8           // Number of loop devices
#define LOOP_DEV_BASE 40  // /dev/loop0 is minor 40

struct loop_device {
  struct spinlock lock;
  struct inode *backing_ip;  // Backing inode (NULL if not configured)
  uint offset;               // Start offset in backing file (supports partitions)
  uint nblocks;              // Size in 512-byte blocks
  int active;                // Is this loop device in use?
};

static struct loop_device loops[NLOOP];
static int loop_inited = 0;

static int
loop_rw(struct buf *b)
{
  struct loop_device *ld;
  int minor;
  uint file_off;
  int r;
  struct inode *ip;
  uint nblocks;

  if(b == 0)
    return -1;

  minor = b->dev - LOOP_DEV_BASE;
  if(minor < 0 || minor >= NLOOP)
    return -1;

  ld = &loops[minor];

  acquire(&ld->lock);
  if(!ld->active || ld->backing_ip == 0){
    release(&ld->lock);
    return -1;
  }

  ip = ld->backing_ip;
  nblocks = ld->nblocks;

  // Check bounds
  if(b->blockno >= nblocks){
    release(&ld->lock);
    return -1;
  }

  file_off = ld->offset + b->blockno * BSIZE;

  // Take a reference and release lock while doing I/O
  ip = idup(ip);
  release(&ld->lock);

  // Lock inode for I/O
  ilock(ip);

  // Use VFS vnode_ops if available (for ext2, etc.), otherwise fall back to xv6fs
  const struct vnode_ops *vops = vfs_dev_vops(ip->dev);
  
  if(vops && vops->read && vops->write){
    // Perform VFS-aware I/O
    if(b->flags & B_DIRTY){
      r = vops->write(ip, (char*)b->data, file_off, BSIZE);
    } else {
      memset(b->data, 0, BSIZE);
      r = vops->read(ip, (char*)b->data, file_off, BSIZE);
    }
  } else {
    // Fallback to xv6fs readi/writei for legacy devices
    if(b->flags & B_DIRTY){
      r = writei(ip, (char*)b->data, file_off, BSIZE);
    } else {
      memset(b->data, 0, BSIZE);
      r = readi(ip, (char*)b->data, file_off, BSIZE);
    }
  }

  iunlockput(ip);

  if(r < 0)
    return -1;

  b->flags |= B_VALID;
  b->flags &= ~B_DIRTY;
  return 0;
}

static uint
loop_nblocks(uint dev)
{
  struct loop_device *ld;
  int minor;
  uint n;

  minor = dev - LOOP_DEV_BASE;
  if(minor < 0 || minor >= NLOOP)
    return 0;

  ld = &loops[minor];
  acquire(&ld->lock);
  n = ld->active ? ld->nblocks : 0;
  release(&ld->lock);
  return n;
}

static const struct bdevsw loop_bdevsw = {
  .rw = loop_rw,
  .nblocks = loop_nblocks,
  .name = "loop",
};

void
loop_init(void)
{
  int i;

  if(loop_inited)
    return;

  for(i = 0; i < NLOOP; i++){
    initlock(&loops[i].lock, "loop");
    loops[i].backing_ip = 0;
    loops[i].offset = 0;
    loops[i].nblocks = 0;
    loops[i].active = 0;
    bdev_register(LOOP_DEV_BASE + i, &loop_bdevsw);
  }

  loop_inited = 1;
  BOOTDBG("loop: initialized %d loop devices (minor %d-%d)\n",
          NLOOP, LOOP_DEV_BASE, LOOP_DEV_BASE + NLOOP - 1);
}

/*
 * Setup a loop device to back an inode.
 * Returns 0 on success, -1 on failure.
 */
int
loop_setup(int loopnum, struct inode *ip, uint offset, uint nblocks)
{
  struct loop_device *ld;

  if(!loop_inited)
    loop_init();

  if(loopnum < 0 || loopnum >= NLOOP)
    return -1;
  if(ip == 0)
    return -1;

  // Get file size to determine nblocks if not specified
  ilock(ip);
  if(nblocks == 0){
    // Auto-detect size from inode
    if(ip->size < BSIZE){
      iunlock(ip);
      return -1;
    }
    nblocks = (ip->size - offset) / BSIZE;
  }
  iunlock(ip);

  if(nblocks == 0)
    return -1;

  ld = &loops[loopnum];
  acquire(&ld->lock);

  if(ld->active){
    release(&ld->lock);
    return -1;  // Already in use
  }

  ld->backing_ip = idup(ip);  // Take a reference
  ld->offset = offset;
  ld->nblocks = nblocks;
  ld->active = 1;

  // Set block count in bdev layer
  bdev_set_nblocks(LOOP_DEV_BASE + loopnum, nblocks);

  release(&ld->lock);

  cprintf("loop: loop%d configured, %d blocks, offset %d\n",
          loopnum, nblocks, offset);
  return 0;
}

/*
 * Release a loop device.
 * Returns 0 on success, -1 on failure.
 */
int
loop_teardown(int loopnum)
{
  struct loop_device *ld;
  struct inode *old_ip;

  if(loopnum < 0 || loopnum >= NLOOP)
    return -1;

  ld = &loops[loopnum];
  acquire(&ld->lock);

  if(!ld->active){
    release(&ld->lock);
    return -1;  // Not active
  }

  old_ip = ld->backing_ip;
  ld->backing_ip = 0;
  ld->offset = 0;
  ld->nblocks = 0;
  ld->active = 0;

  release(&ld->lock);

  // Release inode reference outside the lock
  if(old_ip)
    iput(old_ip);

  cprintf("loop: loop%d released\n", loopnum);
  return 0;
}

/*
 * Get status of a loop device.
 * Returns 1 if active, 0 if not, -1 on error.
 */
int
loop_status(int loopnum, uint *backing_inum, uint *nblocks)
{
  struct loop_device *ld;
  int active;

  if(loopnum < 0 || loopnum >= NLOOP)
    return -1;

  ld = &loops[loopnum];
  acquire(&ld->lock);
  active = ld->active;
  if(active){
    if(backing_inum && ld->backing_ip)
      *backing_inum = ld->backing_ip->inum;
    if(nblocks)
      *nblocks = ld->nblocks;
  }
  release(&ld->lock);

  return active;
}

/*
 * Get the device number for a loop device.
 */
int
loop_devnum(int loopnum)
{
  if(loopnum < 0 || loopnum >= NLOOP)
    return -1;
  return LOOP_DEV_BASE + loopnum;
}

/*
 * Find first free loop device.
 * Returns loop number (0-7) or -1 if none available.
 */
int
loop_find_free(void)
{
  int i;

  if(!loop_inited)
    loop_init();

  for(i = 0; i < NLOOP; i++){
    acquire(&loops[i].lock);
    if(!loops[i].active){
      release(&loops[i].lock);
      return i;
    }
    release(&loops[i].lock);
  }
  return -1;
}
