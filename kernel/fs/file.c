//
// File descriptors
//

#include "types.h"
#include "param.h"
#include "defs.h"
#include "stat.h"
#include "fs.h"
#include "vfs.h"
#include "spinlock.h"
#include "sleeplock.h"
#include "file.h"
#include "proc.h"

struct devsw devsw[NDEV];

// Phase 1A: Global file descriptor pool (kmalloc-backed allocation)
// No longer using a static global file-table array.
// Individual file objects are allocated on demand and placed in per-process fdtable.

// Sleeplock for reference counting operations on global file objects
struct {
  struct sleeplock lock;
} file_global;

void
fileinit(void)
{
  initsleeplock(&file_global.lock, "file_global");
  lockdep_set_rank(&file_global.lock.lk, LOCK_RANK_FTABLE_INTERNAL, "ftable_internal");
}

// Allocate a file structure (Phase 1A: kmalloc-backed instead of array scan)
struct file*
filealloc(void)
{
  struct file *f;

  f = kmalloc(sizeof(*f));
  if(f == 0)
    return 0;

  acquiresleep(&file_global.lock);
  memset(f, 0, sizeof(*f));
  f->ref = 1;
  f->pty_side = PTY_SIDE_NONE;
  f->pty_index = -1;
  releasesleep(&file_global.lock);
  return f;
}

// Increment ref count for file f.
struct file*
filedup(struct file *f)
{
  acquiresleep(&file_global.lock);
  if(f->ref < 1)
    panic("filedup");
  f->ref++;
  releasesleep(&file_global.lock);
  return f;
}

// Close file f.  (Decrement ref count, kfree when reaches 0.)
void
fileclose(struct file *f)
{
  struct file ff;

  acquiresleep(&file_global.lock);
  if(f->ref < 1)
    panic("fileclose");
  if(--f->ref > 0){
    releasesleep(&file_global.lock);
    return;
  }
  ff = *f;
  f->ref = 0;
  f->type = FD_NONE;
  releasesleep(&file_global.lock);

  // Cleanup based on file type
  if(ff.type == FD_PIPE)
    pipeclose(ff.pipe, ff.writable);
  else if(ff.type == FD_INODE){
    if(ff.ip && ff.ip->type == T_DEV && ff.ip->major == PTYDEV)
      pty_close(&ff);
    if(ff.ip && ff.ip->type == T_DEV && ff.ip->major == SERIALDEV)
      serial_close(&ff);
    if(ff.ip && ff.ip->type == T_DEV && ff.ip->major == AUDIODEV)
      audio_close(&ff);
    begin_op();
    iput(ff.ip);
    end_op();
  } else if(ff.type == FD_SOCKET){
    socket_close(ff.socket);
  }

  // Phase 1A: Free the kmalloc'd file object
  kmalloc_free(f);
}

// Get metadata about file f.
int
filestat(struct file *f, struct stat *st)
{
  const struct vnode_ops *ops;
  int rc;

  if(f->type == FD_INODE){
    ilock(f->ip);
    ops = vfs_dev_vops(f->ip->dev);
    if(ops && ops->stat){
      rc = ops->stat(f->ip, st);
    } else {
      rc = 0;
      stati(f->ip, st);
    }
    if(rc == 0 && f->ip->type == T_DEV && f->ip->major == PTYDEV &&
       f->pty_side == PTY_SIDE_SLAVE && f->pty_index >= 0)
      st->st_minor = PTY_MINOR_SLAVE_BASE + f->pty_index;
    iunlock(f->ip);
    return rc;
  }
  return -1;
}

// Read from file f.
int
fileread(struct file *f, char *addr, int n)
{
  int r;
  const struct vnode_ops *ops;
  struct inode *mountpoint;

  if(f->readable == 0)
    return -1;
  if(f->type == FD_PIPE)
    return piperead(f->pipe, addr, n);
  if(f->type == FD_INODE){
    VFSDBG("vfs: fileread dev=%d ino=%d off=%d n=%d\n",
           f->ip->dev, f->ip->inum, (int)f->off, n);
    ilock(f->ip);

    if(f->ip->type == T_DEV && f->ip->major == PTYDEV){
      iunlock(f->ip);
      VFSDBG("vfs: fileread dev=%d ino=%d r=%d off=%d\n",
             f->ip->dev, f->ip->inum, n, (int)f->off);
      return pty_fileread(f, addr, n);
    }

    if(f->ip->type == T_DEV){
      r = readi(f->ip, addr, f->off, n);
      if(r > 0)
        f->off += r;
      iunlock(f->ip);
      VFSDBG("vfs: fileread dev=%d ino=%d r=%d off=%d\n",
             f->ip->dev, f->ip->inum, r, (int)f->off);
      return r;
    }

    // For directories at mount roots, synthesize . and .. entries
    if(f->ip->type == T_DIR && n == sizeof(struct dirent)){
      mountpoint = vfs_mount_crossover(f->ip, 0);
      if(mountpoint){
        // This is a mount root - synthesize . and .. entries
        if(f->off == 0){
          // Entry 0: "." pointing to mount root itself
          struct dirent de;
          memset(&de, 0, sizeof(de));
          de.inum = (ushort)(f->ip->inum & 0xFFFF);
          if(de.inum == 0)
            de.inum = 1;
          de.name[0] = '.';
          memmove(addr, &de, sizeof(de));
          f->off += sizeof(de);
          iput(mountpoint);
          iunlock(f->ip);
             VFSDBG("vfs: fileread dev=%d ino=%d r=%d off=%d\n",
               f->ip->dev, f->ip->inum, (int)sizeof(de), (int)f->off);
          return sizeof(de);
        }
        if(f->off == sizeof(struct dirent)){
          // Entry 1: ".." pointing to parent in underlying fs
          struct dirent de;
          memset(&de, 0, sizeof(de));
          de.inum = (ushort)(mountpoint->inum & 0xFFFF);
          if(de.inum == 0)
            de.inum = 1;
          de.name[0] = '.';
          de.name[1] = '.';
          memmove(addr, &de, sizeof(de));
          f->off += sizeof(de);
          iput(mountpoint);
          iunlock(f->ip);
             VFSDBG("vfs: fileread dev=%d ino=%d r=%d off=%d\n",
               f->ip->dev, f->ip->inum, (int)sizeof(de), (int)f->off);
          return sizeof(de);
        }
        iput(mountpoint);
        // For entries beyond . and .., adjust offset and fall through to fs read
        // The fs will read from offset 0 when we ask for adjusted offset
        ops = vfs_dev_vops(f->ip->dev);
        if(ops && ops->read){
          uint64_t adj_off = f->off - 2 * sizeof(struct dirent);
          r = ops->read(f->ip, addr, adj_off, n);
          if(r > 0)
            f->off += r;
          iunlock(f->ip);
          VFSDBG("vfs: fileread dev=%d ino=%d r=%d off=%d\n",
                 f->ip->dev, f->ip->inum, r, (int)f->off);
          return r;
        }
      }
    }
    
    // Try VFS vnode_ops if device is mounted in VFS
    ops = vfs_dev_vops(f->ip->dev);
    if(ops && ops->read) {
      // Device is mounted; check read capability
      if(!vfs_dev_has_cap(f->ip->dev, VFS_CAP_READ)) {
        iunlock(f->ip);
        return -1;
      }
      r = ops->read(f->ip, addr, f->off, n);
    } else {
      // Fall back to readi for character/block devices not in VFS
      r = readi(f->ip, addr, f->off, n);
    }
    
    if(r > 0)
      f->off += r;
    iunlock(f->ip);
    VFSDBG("vfs: fileread dev=%d ino=%d r=%d off=%d\n",
           f->ip->dev, f->ip->inum, r, (int)f->off);
    return r;
  }
  panic("fileread");
}

//PAGEBREAK!
// Write to file f.
int
filewrite(struct file *f, char *addr, int n)
{
  int r;
  const struct vnode_ops *ops;

  if(f->writable == 0)
    return -1;
  if(f->type == FD_PIPE)
    return pipewrite(f->pipe, addr, n);
  if(f->type == FD_INODE){
    VFSDBG("vfs: filewrite dev=%d ino=%d off=%d n=%d\n",
           f->ip->dev, f->ip->inum, (int)f->off, n);
    if(f->ip->type == T_DEV && f->ip->major == PTYDEV)
      return pty_filewrite(f, addr, n);
    if(f->ip->type == T_DEV && f->ip->major == AUDIODEV)
      return audio_filewrite(f, addr, n);

    if(f->ip->type == T_DEV){
      ilock(f->ip);
      r = writei(f->ip, addr, f->off, n);
      if(r > 0)
        f->off += r;
      iunlock(f->ip);
      VFSDBG("vfs: filewrite dev=%d ino=%d r=%d off=%d\n",
             f->ip->dev, f->ip->inum, r, (int)f->off);
      return r;
    }

    // write a few blocks at a time to avoid exceeding
    // the maximum log transaction size, including
    // i-node, indirect block, allocation blocks,
    // and 2 blocks of slop for non-aligned writes.
    // this really belongs lower down, since writei()
    // might be writing a device like the console.
    int max = ((MAXOPBLOCKS-1-1-2) / 2) * 512;
    int i = 0;
    while(i < n){
      int n1 = n - i;
      if(n1 > max)
        n1 = max;

      begin_op();
      ilock(f->ip);
      
      // Try VFS vnode_ops if device is mounted in VFS
      ops = vfs_dev_vops(f->ip->dev);
      if(ops && ops->write) {
        // Device is mounted; check write capability
        if(!vfs_dev_has_cap(f->ip->dev, VFS_CAP_WRITE)) {
          r = -1;
        } else {
          r = ops->write(f->ip, addr + i, f->off, n1);
        }
      } else {
        // Fall back to writei for character/block devices not in VFS
        r = writei(f->ip, addr + i, f->off, n1);
      }
      
      if(r > 0)
        f->off += r;
      iunlock(f->ip);
      end_op();

      if(r < 0)
        break;
      if(r != n1)
        panic("short filewrite");
      i += r;
    }
    VFSDBG("vfs: filewrite dev=%d ino=%d r=%d off=%d\n",
           f->ip->dev, f->ip->inum, (i == n) ? n : -1, (int)f->off);
    return i == n ? n : -1;
  }
  panic("filewrite");
}

// The function file_has_refs_on_dev has been removed to avoid ptable access issues.

