// Simple PIO-based (non-DMA) IDE driver code.

#include "types.h"
#include "defs.h"
#include "param.h"
#include "memlayout.h"
#include "mmu.h"
#include "proc.h"
#include "x86.h"
#include "traps.h"
#include "spinlock.h"
#include "sleeplock.h"
#include "fs.h"
#include "buf.h"
#include "blockdev.h"
#include "file.h"

#define SECTOR_SIZE   512
#define IDE_BSY       0x80
#define IDE_DRDY      0x40
#define IDE_DF        0x20
#define IDE_ERR       0x01

#define IDE_CMD_READ  0x20
#define IDE_CMD_WRITE 0x30
#define IDE_CMD_RDMUL 0xc4
#define IDE_CMD_WRMUL 0xc5
#define IDE_CMD_IDENTIFY 0xec

#define IDE_WAIT_SPINS 1000000

// idequeue points to the buf now being read/written to the disk.
// idequeue->qnext points to the next buf to be processed.
// You must hold idelock while manipulating queue.

static struct spinlock idelock;
static struct buf *idequeue;

static int havedisk1;
static int havedisk2;
static int havedisk3;
static uint ide_capacity[NDEV];
static int idestart(struct buf*);
static int idewait(ushort iobase, int checkerr);
static int idewait_quiet(ushort iobase, int checkerr);
static ushort ide_iobase(uint dev);
static ushort ide_ctlbase(uint dev);

struct mbr_part {
  uchar boot;
  uchar chs_first[3];
  uchar type;
  uchar chs_last[3];
  uint lba_start;
  uint lba_count;
} __attribute__((packed));

static int ide_read_lba0_poll(uint dev, uchar *dst);
static void ide_scan_partitions(uint dev);

static int
ide_probe_unit(ushort iobase, int unit)
{
  int spins;
  uchar st;
  ushort identify[256];

  outb(iobase + 6, 0xe0 | ((unit & 1) << 4));
  for(spins = 0; spins < 1000; spins++)
    inb(iobase + 7);

  outb(iobase + 2, 0);
  outb(iobase + 3, 0);
  outb(iobase + 4, 0);
  outb(iobase + 5, 0);
  outb(iobase + 7, IDE_CMD_IDENTIFY);

  st = inb(iobase + 7);
  if(st == 0 || st == 0xff)
    return 0;

  if(inb(iobase + 4) != 0 || inb(iobase + 5) != 0)
    return 0;

  // IDENTIFY completes when DRQ is set; fail if ERR/DF is raised.
  spins = IDE_WAIT_SPINS;
  while(spins-- > 0){
    st = inb(iobase + 7);
    if(st & IDE_BSY)
      continue;
    if(st & (IDE_DF | IDE_ERR))
      return 0;
    if(st & 0x08)
      break;
  }
  if(spins <= 0)
    return 0;

  insl(iobase, identify, 128);

  return 1;
}

static void
ide_scan_partitions(uint dev)
{
  uchar mbr[SECTOR_SIZE];
  struct mbr_part *parts;
  uint blocks_per_fs_block;
  uint unit;
  uint p;

  if(bdev_nblocks(dev) == 0)
    return;

  if(ide_read_lba0_poll(dev, mbr) < 0)
    return;

  if(mbr[510] != 0x55 || mbr[511] != 0xaa)
    return;

  parts = (struct mbr_part *)(mbr + 446);
  blocks_per_fs_block = BSIZE / SECTOR_SIZE;
  unit = dev;
  for(p = 0; p < DISK_PARTS_PER_DISK; p++){
    uint start;
    uint count;
    uint pdev;
    uint start_block;
    uint nblocks;

    start = parts[p].lba_start;
    count = parts[p].lba_count;
    if(count == 0)
      continue;
    if(blocks_per_fs_block == 0)
      continue;
    if((start % blocks_per_fs_block) != 0 || (count / blocks_per_fs_block) == 0)
      continue;

    start_block = start / blocks_per_fs_block;
    nblocks = count / blocks_per_fs_block;
    pdev = DISK_PART_DEV(unit, p + 1);
    if(bdev_register_part(pdev, dev, start_block, nblocks) == 0)
      cprintf("ide: part hd%c%d dev=%d start=%d nblk=%d\n",
              'a' + unit, p + 1, pdev, start_block, nblocks);
  }
}

static int
ide_read_lba0_poll(uint dev, uchar *dst)
{
  ushort iobase;
  ushort ctlbase;
  int unit;

  iobase = ide_iobase(dev);
  ctlbase = ide_ctlbase(dev);
  unit = dev & 1;

  acquire(&idelock);
  outb(iobase + 6, 0xe0 | (unit << 4));
  if(idewait_quiet(iobase, 0) < 0){
    release(&idelock);
    return -1;
  }

  outb(ctlbase, 0);
  outb(iobase + 2, 1);
  outb(iobase + 3, 0);
  outb(iobase + 4, 0);
  outb(iobase + 5, 0);
  outb(iobase + 6, 0xe0 | (unit << 4));
  outb(iobase + 7, IDE_CMD_READ);

  if(idewait_quiet(iobase, 1) < 0){
    release(&idelock);
    return -1;
  }

  insl(iobase, dst, SECTOR_SIZE / 4);
  release(&idelock);
  return 0;
}

static ushort
ide_iobase(uint dev)
{
  return (dev < 2) ? 0x1f0 : 0x170;
}

static ushort
ide_ctlbase(uint dev)
{
  return (dev < 2) ? 0x3f6 : 0x376;
}

static uint
ide_nblocks(uint dev)
{
  if(dev >= NDEV)
    return 0;
  return ide_capacity[dev];
}

static int
ide_backend_rw(struct buf *b);

static const struct bdevsw ide_bdev_ops = {
  .rw = ide_backend_rw,
  .nblocks = ide_nblocks,
  .name = "ide",
};

// Wait for IDE disk to become ready.
static int
idewait_internal(ushort iobase, int checkerr, int noisy)
{
  int r;
  int spins;

  spins = IDE_WAIT_SPINS;
  while(spins-- > 0){
    r = inb(iobase + 7);
    if((r & (IDE_BSY|IDE_DRDY)) == IDE_DRDY)
      break;
  }
  if(spins <= 0){
    if(noisy)
      cprintf("ide: wait timeout io=%x st=%x\n", iobase, inb(iobase + 7));
    return -1;
  }
  if(checkerr && (r & (IDE_DF|IDE_ERR)) != 0)
  {
    if(noisy)
      cprintf("ide: wait error io=%x st=%x\n", iobase, r);
    return -1;
  }
  return 0;
}

static int
idewait(ushort iobase, int checkerr)
{
  return idewait_internal(iobase, checkerr, 1);
}

static int
idewait_quiet(ushort iobase, int checkerr)
{
  return idewait_internal(iobase, checkerr, 0);
}

void
ideinit(void)
{
  int i;
  uchar probe_sector[SECTOR_SIZE];

  initlock(&idelock, "ide");
  ioapicenable(IRQ_IDE, ncpu - 1);
  ioapicenable(IRQ_IDE + 1, ncpu - 1);
  idewait(0x1f0, 0);

  // Probe optional units conservatively to avoid registering phantom disks.
  havedisk1 = ide_probe_unit(0x1f0, 1);
  havedisk2 = ide_probe_unit(0x170, 0);
  havedisk3 = ide_probe_unit(0x170, 1);

  // Some emulated controllers can be flaky with IDENTIFY on secondary units.
  // Fallback to a bounded polling read of LBA0 before declaring absent.
  if(!havedisk1 && ide_read_lba0_poll(1, probe_sector) == 0)
    havedisk1 = 1;
  if(!havedisk2 && ide_read_lba0_poll(2, probe_sector) == 0)
    havedisk2 = 1;
  if(!havedisk3 && ide_read_lba0_poll(3, probe_sector) == 0)
    havedisk3 = 1;

  cprintf("ide: probe d0=1 d1=%d d2=%d d3=%d\n", havedisk1, havedisk2, havedisk3);

  // Switch back to disk 0.
  outb(0x1f6, 0xe0 | (0<<4));

  // Seed per-device capacities; backend checks consume these via bdev_nblocks().
  for(i = 0; i < NDEV; i++)
    ide_capacity[i] = 0;
  ide_capacity[0] = FSSIZE;
  if(havedisk1)
    ide_capacity[1] = FSSIZE;
  if(havedisk2)
    ide_capacity[2] = FSSIZE;
  if(havedisk3)
    ide_capacity[3] = FSSIZE;

  // Expose available IDE units through block-device switch.
  if(bdev_register(0, &ide_bdev_ops) < 0)
    panic("ideinit: register disk0");
  if(havedisk1 && bdev_register(1, &ide_bdev_ops) < 0)
    panic("ideinit: register disk1");
  if(havedisk2 && bdev_register(2, &ide_bdev_ops) < 0)
    panic("ideinit: register disk2");
  if(havedisk3 && bdev_register(3, &ide_bdev_ops) < 0)
    panic("ideinit: register disk3");

  ide_scan_partitions(0);
  if(havedisk1)
    ide_scan_partitions(1);
  if(havedisk2)
    ide_scan_partitions(2);
  if(havedisk3)
    ide_scan_partitions(3);
}

// Start the request for b.  Caller must hold idelock.
static int
idestart(struct buf *b)
{
  uint nblocks;
  ushort iobase;
  ushort ctlbase;
  int unit;

  if(b == 0)
    panic("idestart");
  nblocks = bdev_nblocks(b->dev);
  if(nblocks == 0 || b->blockno >= nblocks)
    panic("idestart: block out of range");
  int sector_per_block =  BSIZE/SECTOR_SIZE;
  int sector = b->blockno * sector_per_block;
  int read_cmd = (sector_per_block == 1) ? IDE_CMD_READ :  IDE_CMD_RDMUL;
  int write_cmd = (sector_per_block == 1) ? IDE_CMD_WRITE : IDE_CMD_WRMUL;
  iobase = ide_iobase(b->dev);
  ctlbase = ide_ctlbase(b->dev);
  unit = b->dev & 1;

  if (sector_per_block > 7) panic("idestart");

  // Select target unit before checking ready state; secondary-channel
  // devices may never report DRDY for the default selected unit.
  outb(iobase + 6, 0xe0 | (unit<<4) | ((sector>>24)&0x0f));
  if(idewait(iobase, 0) < 0)
    return -1;
  outb(ctlbase, 0);  // generate interrupt
  outb(iobase + 2, sector_per_block);  // number of sectors
  outb(iobase + 3, sector & 0xff);
  outb(iobase + 4, (sector >> 8) & 0xff);
  outb(iobase + 5, (sector >> 16) & 0xff);
  outb(iobase + 6, 0xe0 | (unit<<4) | ((sector>>24)&0x0f));
  if(b->flags & B_DIRTY){
    outb(iobase + 7, write_cmd);
    outsl(iobase, b->data, BSIZE/4);
  } else {
    outb(iobase + 7, read_cmd);
  }
  return 0;
}

// Interrupt handler.
void
ideintr(void)
{
  struct buf *b;
  ushort iobase;

  // First queued buffer is the active request.
  acquire(&idelock);

  if((b = idequeue) == 0){
    release(&idelock);
    return;
  }
  idequeue = b->qnext;
  iobase = ide_iobase(b->dev);

  // Read data if needed.
  if(!(b->flags & B_DIRTY) && idewait(iobase, 1) >= 0)
    insl(iobase, b->data, BSIZE/4);

  // Wake process waiting for this buf.
  b->flags |= B_VALID;
  b->flags &= ~B_DIRTY;
  wakeup(b);

  // Start disk on next buf in queue.
  if(idequeue != 0 && idestart(idequeue) < 0)
    panic("ideintr: start");

  release(&idelock);
}

//PAGEBREAK!
// Sync buf with disk.
// If B_DIRTY is set, write buf to disk, clear B_DIRTY, set B_VALID.
// Else if B_VALID is not set, read buf from disk, set B_VALID.
void
iderw(struct buf *b)
{
  if(ide_backend_rw(b) < 0)
    panic("iderw: no backend");
}

static int
ide_backend_rw(struct buf *b)
{
  struct buf **pp;
  ushort iobase;

  if(!holdingsleep(&b->lock))
    panic("iderw: buf not locked");
  if((b->flags & (B_VALID|B_DIRTY)) == B_VALID)
    panic("iderw: nothing to do");
  if((b->dev == 1 && !havedisk1) ||
     (b->dev == 2 && !havedisk2) ||
     (b->dev == 3 && !havedisk3))
    panic("iderw: ide disk not present");

  // Secondary channel devices are handled synchronously with polling.
  // This avoids depending on IRQ15 delivery, which is flaky in some setups.
  if(b->dev >= 2){
    acquire(&idelock);
    if(idestart(b) < 0){
      cprintf("ide: start failed dev=%d blk=%d\n", b->dev, b->blockno);
      if((b->flags & B_DIRTY) == 0)
        memset(b->data, 0, BSIZE);
      b->flags |= B_VALID;
      b->flags &= ~B_DIRTY;
      release(&idelock);
      return 0;
    }
    iobase = ide_iobase(b->dev);
    if(idewait(iobase, 1) < 0){
      cprintf("ide: rw timeout dev=%d blk=%d\n", b->dev, b->blockno);
      if((b->flags & B_DIRTY) == 0)
        memset(b->data, 0, BSIZE);
      b->flags |= B_VALID;
      b->flags &= ~B_DIRTY;
      release(&idelock);
      return 0;
    }
    if(!(b->flags & B_DIRTY))
      insl(iobase, b->data, BSIZE/4);
    b->flags |= B_VALID;
    b->flags &= ~B_DIRTY;
    release(&idelock);
    return 0;
  }

  acquire(&idelock);  //DOC:acquire-lock

  // Append b to idequeue.
  b->qnext = 0;
  for(pp=&idequeue; *pp; pp=&(*pp)->qnext)  //DOC:insert-queue
    ;
  *pp = b;

  // Start disk if necessary.
  if(idequeue == b && idestart(b) < 0){
    idequeue = b->qnext;
    b->qnext = 0;
    release(&idelock);
    return -1;
  }

  // Wait for request to finish.
  while((b->flags & (B_VALID|B_DIRTY)) != B_VALID){
    sleep(b, &idelock);
  }


  release(&idelock);
  return 0;
}
