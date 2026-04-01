//
// File descriptors
//

#include "types.h"
#include "defs.h"
#include "param.h"
#include "stat.h"
#include "fs.h"
#include "vfs.h"
#include "spinlock.h"
#include "sleeplock.h"
#include "file.h"

struct devsw devsw[NDEV];
struct {
  struct spinlock lock;
  struct file file[NFILE];
} ftable;

void
fileinit(void)
{
  initlock(&ftable.lock, "ftable");
}

// Allocate a file structure.
struct file*
filealloc(void)
{
  struct file *f;

  acquire(&ftable.lock);
  for(f = ftable.file; f < ftable.file + NFILE; f++){
    if(f->ref == 0){
      f->ref = 1;
      f->pty_side = PTY_SIDE_NONE;
      f->pty_index = -1;
      release(&ftable.lock);
      return f;
    }
  }
  release(&ftable.lock);
  return 0;
}

// Increment ref count for file f.
struct file*
filedup(struct file *f)
{
  acquire(&ftable.lock);
  if(f->ref < 1)
    panic("filedup");
  f->ref++;
  release(&ftable.lock);
  return f;
}

// Close file f.  (Decrement ref count, close when reaches 0.)
void
fileclose(struct file *f)
{
  struct file ff;

  acquire(&ftable.lock);
  if(f->ref < 1)
    panic("fileclose");
  if(--f->ref > 0){
    release(&ftable.lock);
    return;
  }
  ff = *f;
  f->ref = 0;
  f->type = FD_NONE;
  release(&ftable.lock);

  if(ff.type == FD_PIPE)
    pipeclose(ff.pipe, ff.writable);
  else if(ff.type == FD_INODE){
    if(ff.ip && ff.ip->type == T_DEV && ff.ip->major == PTYDEV)
      pty_close(&ff);
    begin_op();
    iput(ff.ip);
    end_op();
  } else if(ff.type == FD_SOCKET){
    socket_close(ff.socket);
  }
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
    ilock(f->ip);

    if(f->ip->type == T_DEV && f->ip->major == PTYDEV){
      iunlock(f->ip);
      return pty_fileread(f, addr, n);
    }

    if(f->ip->type == T_DEV){
      r = readi(f->ip, addr, f->off, n);
      if(r > 0)
        f->off += r;
      iunlock(f->ip);
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
          return sizeof(de);
        }
        iput(mountpoint);
        // For entries beyond . and .., adjust offset and fall through to fs read
        // The fs will read from offset 0 when we ask for adjusted offset
        ops = vfs_dev_vops(f->ip->dev);
        if(ops && ops->read){
          uint adj_off = f->off - 2 * sizeof(struct dirent);
          r = ops->read(f->ip, addr, adj_off, n);
          if(r > 0)
            f->off += r;
          iunlock(f->ip);
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
    if(f->ip->type == T_DEV && f->ip->major == PTYDEV)
      return pty_filewrite(f, addr, n);

    if(f->ip->type == T_DEV){
      ilock(f->ip);
      r = writei(f->ip, addr, f->off, n);
      if(r > 0)
        f->off += r;
      iunlock(f->ip);
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
    return i == n ? n : -1;
  }
  panic("filewrite");
}

int
file_has_refs_on_dev(uint dev)
{
  struct file *f;

  acquire(&ftable.lock);
  for(f = ftable.file; f < ftable.file + NFILE; f++){
    if(f->ref < 1)
      continue;
    if(f->type != FD_INODE || f->ip == 0)
      continue;
    if(f->ip->dev == dev){
      release(&ftable.lock);
      return 1;
    }
  }
  release(&ftable.lock);
  return 0;
}

