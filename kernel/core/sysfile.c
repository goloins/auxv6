//
// File-system system calls.
// Mostly argument checking, since we don't trust
// user code, and calls into file.c and fs.c.
//

#include "types.h"
#include "defs.h"
#include "param.h"
#include "stat.h"
#include "mmu.h"
#include "proc.h"
#include "fs.h"
#include "spinlock.h"
#include "sleeplock.h"
#include "file.h"
#include "fcntl.h"
#include "vfs.h"

#define DEFAULT_CREATE_FILE_MODE (M_IRUSR | M_IWUSR | M_IRGRP | M_IROTH)
#define DEFAULT_CREATE_DIR_MODE (M_IRUSR | M_IWUSR | M_IXUSR | M_IRGRP | M_IXGRP | M_IROTH | M_IXOTH)
#define DEFAULT_CREATE_DEV_MODE (M_IRUSR | M_IWUSR | M_IRGRP | M_IWGRP | M_IROTH | M_IWOTH)
#define GETCWD_MAX_DEPTH 32

static int create_default_mode(short type);
static int create_device_mode(int mode);
static int inode_dir_read(struct inode *dp, struct dirent *de, uint off);
static struct inode* inode_dir_lookup(struct inode *dp, char *name, uint *poff);
static int child_name_in_parent(struct inode *parent, uint child_inum, char *name);
static int buildcwd(struct inode *cwd, char *buf, int size);
static int inode_is_owner_or_root(struct inode *ip);
static int inode_is_root_user(void);
static struct inode* vfs_resolve(char *path);
static struct inode* vfs_resolve_parent(char *path, char *name);

static int
create_default_mode(short type)
{
  if(type == T_DIR)
    return DEFAULT_CREATE_DIR_MODE;
  return DEFAULT_CREATE_FILE_MODE;
}

static int
create_device_mode(int mode)
{
  int kind;
  int perms;

  kind = mode & M_IFMT;
  if(kind != M_IFCHR && kind != M_IFBLK)
    kind = M_IFCHR;

  perms = mode & 07777;
  if(perms == 0)
    perms = DEFAULT_CREATE_DEV_MODE;

  return kind | perms;
}

static int
inode_dir_read(struct inode *dp, struct dirent *de, uint off)
{
  const struct vnode_ops *ops;

  if(dp == 0 || de == 0)
    return -1;

  ops = vfs_dev_vops(dp->dev);
  if(ops && ops->read)
    return ops->read(dp, (char*)de, off, sizeof(*de));
  return readi(dp, (char*)de, off, sizeof(*de));
}

static struct inode*
inode_dir_lookup(struct inode *dp, char *name, uint *poff)
{
  const struct vnode_ops *ops;

  if(dp == 0 || name == 0)
    return 0;

  ops = vfs_dev_vops(dp->dev);
  if(ops && ops->dirlookup)
    return ops->dirlookup(dp, name, poff);
  return dirlookup(dp, name, poff);
}

static int
child_name_in_parent(struct inode *parent, uint child_inum, char *name)
{
  uint off;
  int i;
  struct dirent de;

  for(off = 0; off < parent->size; off += sizeof(de)){
    if(inode_dir_read(parent, &de, off) != sizeof(de))
      return -1;
    if(de.inum != child_inum)
      continue;
    memmove(name, de.name, DIRSIZ);
    name[DIRSIZ] = 0;
    for(i = 0; i < DIRSIZ; i++){
      if(name[i] == 0)
        break;
    }
    name[i] = 0;
    if(namecmp(name, ".") == 0 || namecmp(name, "..") == 0)
      continue;
    return 0;
  }

  return -1;
}

static int
buildcwd(struct inode *cwd, char *buf, int size)
{
  struct inode *ip;
  struct inode *parent;
  struct inode *cross;
  char parts[GETCWD_MAX_DEPTH][DIRSIZ + 1];
  int depth;
  int i;
  int len;

  if(cwd == 0 || buf == 0 || size < 2)
    return -1;

  ip = idup(cwd);
  depth = 0;

  for(;;){
    ilock(ip);
    if(vfs_is_root_inode(ip)){
      iunlock(ip);
      break;
    }
    if(ip->type != T_DIR){
      iunlockput(ip);
      return -1;
    }
    // Check for mount boundary crossing
    iunlock(ip);
    cross = vfs_mount_crossover(ip, 0);
    if(cross != 0){
      // We're at a mount root - cross to underlying filesystem
      // Don't record name here; let normal parent walk find it
      iput(ip);
      ip = cross;
      continue;
    }
    ilock(ip);
    parent = inode_dir_lookup(ip, "..", 0);
    iunlock(ip);
    if(parent == 0){
      iput(ip);
      return -1;
    }
    // Check if parent is same as ip (stuck at a root)
    if(parent->dev == ip->dev && parent->inum == ip->inum){
      iput(parent);
      // Try mount crossover as fallback
      cross = vfs_mount_crossover(ip, 0);
      if(cross != 0){
        iput(ip);
        ip = cross;
        continue;
      }
      iput(ip);
      return -1;
    }
    ilock(parent);
    if(parent->type != T_DIR || depth >= GETCWD_MAX_DEPTH ||
       child_name_in_parent(parent, ip->inum, parts[depth]) < 0){
      iunlockput(parent);
      iput(ip);
      return -1;
    }
    depth++;
    iunlock(parent);
    iput(ip);
    ip = parent;
  }
  iput(ip);

  if(depth == 0){
    buf[0] = '/';
    buf[1] = 0;
    return 0;
  }

  len = 0;
  for(i = depth - 1; i >= 0; i--){
    int plen;

    plen = strlen(parts[i]);
    if(plen <= 0)
      continue;
    if(len + 1 + plen >= size)
      return -1;
    buf[len++] = '/';
    memmove(buf + len, parts[i], plen);
    len += plen;
  }
  buf[len] = 0;
  return 0;
}

static int
inode_is_owner_or_root(struct inode *ip)
{
  struct proc *p;

  p = myproc();
  if(p == 0)
    return 0;
  return p->uid == 0 || p->uid == ip->uid;
}

static int
inode_is_root_user(void)
{
  struct proc *p;

  p = myproc();
  return p != 0 && p->uid == 0;
}

static struct inode*
vfs_resolve(char *path)
{
  struct vnode vn;

  if(vfs_lookup(path, &vn) < 0)
    return 0;
  return vn.ip;
}

static struct inode*
vfs_resolve_parent(char *path, char *name)
{
  struct vnode vn;

  if(vfs_lookup_parent(path, name, &vn) < 0)
    return 0;
  return vn.ip;
}

// Fetch the nth word-sized system call argument as a file descriptor
// and return both the descriptor and the corresponding struct file.
static int
argfd(int n, int *pfd, struct file **pf)
{
  int fd;
  struct file *f;

  if(argint(n, &fd) < 0)
    return -1;
  if(fd < 0 || fd >= NOFILE || (f=myproc()->ofile[fd]) == 0)
    return -1;
  if(pfd)
    *pfd = fd;
  if(pf)
    *pf = f;
  return 0;
}

// Allocate a file descriptor for the given file.
// Takes over file reference from caller on success.
static int
fdalloc(struct file *f)
{
  int fd;
  struct proc *curproc = myproc();

  for(fd = 0; fd < NOFILE; fd++){
    if(curproc->ofile[fd] == 0){
      curproc->ofile[fd] = f;
      return fd;
    }
  }
  return -1;
}

int
sys_dup(void)
{
  struct file *f;
  int fd;

  if(argfd(0, 0, &f) < 0)
    return -1;
  if((fd=fdalloc(f)) < 0)
    return -1;
  filedup(f);
  return fd;
}

int
sys_read(void)
{
  struct file *f;
  int n;
  char *p;

  if(argfd(0, 0, &f) < 0 || argint(2, &n) < 0 || argptr(1, &p, n) < 0)
    return -1;
  return fileread(f, p, n);
}

int
sys_getdents(void)
{
  struct file *f;
  struct dirent *ents;
  struct dirent de;
  int max;
  int out;
  int r;

  if(argfd(0, 0, &f) < 0 || argint(2, &max) < 0)
    return -1;
  if(max < 0)
    return -1;
  if(max > PGSIZE / sizeof(*ents))
    return -1;
  if(argptr(1, (char**)&ents, max * sizeof(*ents)) < 0)
    return -1;

  if(f->type != FD_INODE)
    return -1;

  ilock(f->ip);
  if(f->ip->type != T_DIR){
    iunlock(f->ip);
    return -1;
  }
  iunlock(f->ip);

  out = 0;
  while(out < max){
    r = fileread(f, (char*)&de, sizeof(de));
    if(r == 0)
      break;
    if(r < 0)
      return (out > 0) ? out : -1;
    if(r != sizeof(de))
      break;
    if(de.inum == 0)
      continue;
    ents[out++] = de;
  }

  return out;
}

int
sys_write(void)
{
  struct file *f;
  int n;
  char *p;

  if(argfd(0, 0, &f) < 0 || argint(2, &n) < 0 || argptr(1, &p, n) < 0)
    return -1;
  return filewrite(f, p, n);
}

int
sys_close(void)
{
  int fd;
  struct file *f;

  if(argfd(0, &fd, &f) < 0)
    return -1;
  myproc()->ofile[fd] = 0;
  fileclose(f);
  return 0;
}

int
sys_fstat(void)
{
  struct file *f;
  struct stat *st;

  if(argfd(0, 0, &f) < 0 || argptr(1, (void*)&st, sizeof(*st)) < 0)
    return -1;
  return filestat(f, st);
}

// stat by path — does not require read permission on the target itself,
// only execute permission on each directory component in the path.
int
sys_stat(void)
{
  char *path;
  const struct vnode_ops *ops;
  int rc;
  struct stat *st;
  struct inode *ip;

  if(argstr(0, &path) < 0 || argptr(1, (void*)&st, sizeof(*st)) < 0)
    return -1;
//big win here
  begin_op();
  if((ip = vfs_resolve(path)) == 0){
    end_op();
    return -1;
  }
  ilock(ip);
  ops = vfs_dev_vops(ip->dev);
  if(ops && ops->stat){
    rc = ops->stat(ip, st);
  } else {
    rc = 0;
    stati(ip, st);
  }
  iunlockput(ip);
  end_op();
  return rc;
}

// Create the path new as a link to the same inode as old.
int
sys_link(void)
{
  const struct vnode_ops *ops;
  char name[DIRSIZ], *new, *old;
  struct inode *dp, *ip;

  if(argstr(0, &old) < 0 || argstr(1, &new) < 0)
    return -1;

  begin_op();
  if((ip = vfs_resolve(old)) == 0){
    end_op();
    return -1;
  }

  ilock(ip);
  if(ip->type == T_DIR){
    iunlockput(ip);
    end_op();
    return -1;
  }

  if((dp = vfs_resolve_parent(new, name)) == 0)
    goto bad_locked;
  ilock(dp);
  if(iaccess(dp, IACC_WRITE | IACC_EXEC) < 0){
    iunlockput(dp);
    goto bad_locked;
  }

  ops = vfs_dev_vops(dp->dev);
  if(ops && ops->link){
    iunlock(ip);
    if(ops->link(ip, dp, name) < 0){
      iunlockput(dp);
      iput(ip);
      end_op();
      return -1;
    }
    iunlockput(dp);
    iput(ip);
    end_op();
    return 0;
  }

  ip->nlink++;
  iupdate(ip);
  iunlock(ip);

  if(dp->dev != ip->dev || dirlink(dp, name, ip->inum) < 0){
    iunlockput(dp);
    goto bad;
  }
  iunlockput(dp);
  iput(ip);

  end_op();

  return 0;

bad:
  ilock(ip);
  ip->nlink--;
  iupdate(ip);
  iunlockput(ip);
  end_op();
  return -1;

bad_locked:
  iunlockput(ip);
  end_op();
  return -1;
}

int
sys_rename(void)
{
  const struct vnode_ops *ops;
  char oldname[DIRSIZ], newname[DIRSIZ], *old, *new;
  struct inode *olddp, *newdp;
  struct inode *first, *second;
  int rc;

  if(argstr(0, &old) < 0 || argstr(1, &new) < 0)
    return -1;
  if(strlen(old) == strlen(new) && strncmp(old, new, strlen(old)) == 0)
    return 0;

  begin_op();

  olddp = vfs_resolve_parent(old, oldname);
  if(olddp == 0)
    goto bad;
  newdp = vfs_resolve_parent(new, newname);
  if(newdp == 0){
    iput(olddp);
    goto bad;
  }

  if(namecmp(oldname, ".") == 0 || namecmp(oldname, "..") == 0 ||
     namecmp(newname, ".") == 0 || namecmp(newname, "..") == 0){
    iput(olddp);
    iput(newdp);
    goto bad;
  }

  if(olddp == newdp){
    ilock(olddp);
    if(iaccess(olddp, IACC_WRITE | IACC_EXEC) < 0){
      iunlockput(olddp);
      goto bad;
    }
    ops = vfs_dev_vops(olddp->dev);
    if(ops == 0 || ops->rename == 0){
      iunlockput(olddp);
      goto bad;
    }
    rc = ops->rename(olddp, oldname, olddp, newname);
    iunlockput(olddp);
    end_op();
    return rc;
  }

  first = olddp;
  second = newdp;
  if((newdp->dev < olddp->dev) ||
     (newdp->dev == olddp->dev && newdp->inum < olddp->inum)){
    first = newdp;
    second = olddp;
  }

  ilock(first);
  ilock(second);

  if(iaccess(olddp, IACC_WRITE | IACC_EXEC) < 0 ||
     iaccess(newdp, IACC_WRITE | IACC_EXEC) < 0){
    iunlockput(second);
    iunlockput(first);
    goto bad;
  }

  ops = vfs_dev_vops(olddp->dev);
  if(ops == 0 || ops->rename == 0){
    iunlockput(second);
    iunlockput(first);
    goto bad;
  }

  rc = ops->rename(olddp, oldname, newdp, newname);
  iunlockput(second);
  iunlockput(first);
  end_op();
  return rc;

bad:
  end_op();
  return -1;
}

// Is the directory dp empty except for "." and ".." ?
static int
isdirempty(struct inode *dp)
{
  int off;
  struct dirent de;

  for(off=2*sizeof(de); off<dp->size; off+=sizeof(de)){
    if(inode_dir_read(dp, &de, off) != sizeof(de))
      panic("isdirempty: readi");
    if(de.inum != 0)
      return 0;
  }
  return 1;
}

//PAGEBREAK!
int
sys_unlink(void)
{
  const struct vnode_ops *ops;
  struct inode *ip, *dp;
  struct dirent de;
  char name[DIRSIZ], *path;
  uint off;

  if(argstr(0, &path) < 0)
    return -1;

  begin_op();
  if((dp = vfs_resolve_parent(path, name)) == 0){
    end_op();
    return -1;
  }

  ilock(dp);
  if(iaccess(dp, IACC_WRITE | IACC_EXEC) < 0)
    goto bad;

  // Cannot unlink "." or "..".
  if(namecmp(name, ".") == 0 || namecmp(name, "..") == 0)
    goto bad;

  ops = vfs_dev_vops(dp->dev);
  if(ops && ops->remove){
    if(ops->remove(dp, name) < 0)
      goto bad;
    iunlockput(dp);
    end_op();
    return 0;
  }

  if((ip = dirlookup(dp, name, &off)) == 0)
    goto bad;
  ilock(ip);

  if(ip->nlink < 1)
    panic("unlink: nlink < 1");
  if(ip->type == T_DIR && !isdirempty(ip)){
    iunlockput(ip);
    goto bad;
  }

  memset(&de, 0, sizeof(de));
  if(writei(dp, (char*)&de, off, sizeof(de)) != sizeof(de))
    panic("unlink: writei");
  if(ip->type == T_DIR){
    dp->nlink--;
    iupdate(dp);
  }
  iunlockput(dp);

  ip->nlink--;
  iupdate(ip);
  iunlockput(ip);

  end_op();

  return 0;

bad:
  iunlockput(dp);
  end_op();
  return -1;
}

static struct inode*
create(char *path, short type, short major, short minor, int mode)
{
  const struct vnode_ops *ops;
  int legacy_xv6;
  int (*dirlink_fn)(struct inode*, char*, uint);
  struct inode* (*create_fn)(struct inode*, char*, short, short, short, int, int, int);
  struct inode *ip, *dp;
  char name[DIRSIZ];

  if((dp = vfs_resolve_parent(path, name)) == 0)
    return 0;
  ilock(dp);

  ops = vfs_dev_vops(dp->dev);
  legacy_xv6 = vfs_dev_is_xv6fs(dp->dev);
  dirlink_fn = dirlink;
  create_fn = 0;
  if(ops && ops->dirlink)
    dirlink_fn = ops->dirlink;
  if(ops && ops->create)
    create_fn = ops->create;

  if(ops && ops->dirlookup)
    ip = ops->dirlookup(dp, name, 0);
  else if(legacy_xv6)
    ip = dirlookup(dp, name, 0);
  else {
    iunlockput(dp);
    return 0;
  }
  if(ip != 0){
    iunlockput(dp);
    ilock(ip);
    if(type == T_FILE && ip->type == T_FILE)
      return ip;
    iunlockput(ip);
    return 0;
  }

  if(create_fn){
    ip = create_fn(dp, name, type, major, minor, mode, myproc()->uid, myproc()->gid);
    iunlockput(dp);
    return ip;
  }

  if(!legacy_xv6){
    iunlockput(dp);
    return 0;
  }

  // Backend does not support creating new directory entries/inodes on this dev.
  if(!vfs_dev_has_cap(dp->dev, VFS_CAP_CREATE)){
    iunlockput(dp);
    return 0;
  }

  if(iaccess(dp, IACC_WRITE | IACC_EXEC) < 0){
    iunlockput(dp);
    return 0;
  }

  if((ip = ialloc(dp->dev, type)) == 0)
    panic("create: ialloc");

  ilock(ip);
  ip->major = major;
  ip->minor = minor;
  ip->nlink = 1;
  ip->uid = myproc()->uid;
  ip->gid = myproc()->gid;
  if(type == T_DEV)
    ip->mode = create_device_mode(mode);
  else
    ip->mode = create_default_mode(type);
  iupdate(ip);

  if(type == T_DIR){  // Create . and .. entries.
    dp->nlink++;  // for ".."
    iupdate(dp);
    // No ip->nlink++ for ".": avoid cyclic ref count.
    if(dirlink_fn(ip, ".", ip->inum) < 0 || dirlink_fn(ip, "..", dp->inum) < 0)
      panic("create dots");
  }

  if(dirlink_fn(dp, name, ip->inum) < 0)
    panic("create: dirlink");

  iunlockput(dp);

  return ip;
}

int
sys_open(void)
{
  char *path;
  int fd, omode;
  int must_write;
  uint startoff;
  struct file *f;
  struct inode *ip;
  const struct vnode_ops *ops;

  if(argstr(0, &path) < 0 || argint(1, &omode) < 0)
    return -1;

  begin_op();

  if(omode & O_CREATE){
    ip = create(path, T_FILE, 0, 0, 0);
    if(ip == 0){
      end_op();
      return -1;
    }
    if((omode & O_RDWR) == O_RDWR) {
      if(iaccess(ip, IACC_READ | IACC_WRITE) < 0){
        iunlockput(ip);
        end_op();
        return -1;
      }
    } else if(omode & O_WRONLY) {
      if(iaccess(ip, IACC_WRITE) < 0){
        iunlockput(ip);
        end_op();
        return -1;
      }
    } else {
      if(iaccess(ip, IACC_READ) < 0){
        iunlockput(ip);
        end_op();
        return -1;
      }
    }
  } else {
    if((ip = vfs_resolve(path)) == 0){
      end_op();
      return -1;
    }
    ilock(ip);
    if((omode & O_RDWR) == O_RDWR) {
      if(iaccess(ip, IACC_READ | IACC_WRITE) < 0){
        iunlockput(ip);
        end_op();
        return -1;
      }
    } else if(omode & O_WRONLY) {
      if(iaccess(ip, IACC_WRITE) < 0){
        iunlockput(ip);
        end_op();
        return -1;
      }
    } else {
      if(iaccess(ip, IACC_READ) < 0){
        iunlockput(ip);
        end_op();
        return -1;
      }
    }
    if(ip->type == T_DIR && omode != O_RDONLY){
      iunlockput(ip);
      end_op();
      return -1;
    }
  }

  if((f = filealloc()) == 0 || (fd = fdalloc(f)) < 0){
    if(f)
      fileclose(f);
    iunlockput(ip);
    end_op();
    return -1;
  }

  must_write = (omode & O_WRONLY) || ((omode & O_RDWR) == O_RDWR);
  if((omode & O_TRUNC) && must_write){
    ops = vfs_dev_vops(ip->dev);
    if(ops && ops->truncate){
      if(ops->truncate(ip) < 0){
        myproc()->ofile[fd] = 0;
        fileclose(f);
        iunlockput(ip);
        end_op();
        return -1;
      }
    }
  }

  startoff = (omode & O_APPEND) ? ip->size : 0;
  iunlock(ip);
  end_op();

  f->type = FD_INODE;
  f->ip = ip;
  f->off = startoff;
  f->readable = !(omode & O_WRONLY);
  f->writable = (omode & O_WRONLY) || (omode & O_RDWR);
  return fd;
}

int
sys_mkdir(void)
{
  char *path;
  struct inode *ip;

  begin_op();
  if(argstr(0, &path) < 0 || (ip = create(path, T_DIR, 0, 0, 0)) == 0){
    end_op();
    return -1;
  }
  iunlockput(ip);
  end_op();
  return 0;
}

int
sys_mknod(void)
{
  struct inode *ip;
  char *path;
    int mode;
  int major, minor;

  begin_op();
  if((argstr(0, &path)) < 0 ||
      argint(1, &mode) < 0 ||
      argint(2, &major) < 0 ||
      argint(3, &minor) < 0 ||
      ((mode & M_IFMT) != M_IFCHR && (mode & M_IFMT) != M_IFBLK) ||
      (ip = create(path, T_DEV, major, minor, mode)) == 0){
    end_op();
    return -1;
  }
  iunlockput(ip);
  end_op();
  return 0;
}

int
sys_chdir(void)
{
  char *path;
  struct inode *ip;
  struct proc *curproc = myproc();
  
  begin_op();
  if(argstr(0, &path) < 0 || (ip = vfs_resolve(path)) == 0){
    end_op();
    return -1;
  }
  ilock(ip);
  if(ip->type != T_DIR || iaccess(ip, IACC_EXEC) < 0){
    iunlockput(ip);
    end_op();
    return -1;
  }
  iunlock(ip);
  iput(curproc->cwd);
  end_op();
  curproc->cwd = ip;
  return 0;
}

int
sys_getcwd(void)
{
  char *buf;
  int size;
  char path[128];

  if(argint(1, &size) < 0)
    return -1;
  if(size <= 1 || argptr(0, &buf, size) < 0)
    return -1;

  begin_op();
  if(buildcwd(myproc()->cwd, path, sizeof(path)) < 0){
    end_op();
    return -1;
  }
  end_op();

  if(strlen(path) + 1 > (uint)size)
    return -1;
  memmove(buf, path, strlen(path) + 1);
  return 0;
}

int
sys_chmod(void)
{
  char *path;
  int mode;
  struct inode *ip;
  const struct vnode_ops *ops;

  if(argstr(0, &path) < 0 || argint(1, &mode) < 0)
    return -1;

  begin_op();
  if((ip = vfs_resolve(path)) == 0){
    end_op();
    return -1;
  }
  ilock(ip);
  if(!inode_is_owner_or_root(ip)){
    iunlockput(ip);
    end_op();
    return -1;
  }
  ops = vfs_dev_vops(ip->dev);
  if(ops && ops->setattr){
    if(ops->setattr(ip, 1, mode, 0, 0, 0, 0) < 0){
      iunlockput(ip);
      end_op();
      return -1;
    }
  } else {
    ip->mode = (ip->mode & M_IFMT) | (mode & 07777);
    iupdate(ip);
  }
  iunlockput(ip);
  end_op();
  return 0;
}

int
sys_chown(void)
{
  char *path;
  int uid;
  int gid;
  struct inode *ip;
  const struct vnode_ops *ops;

  if(argstr(0, &path) < 0 || argint(1, &uid) < 0 || argint(2, &gid) < 0)
    return -1;

  begin_op();
  if((ip = vfs_resolve(path)) == 0){
    end_op();
    return -1;
  }
  ilock(ip);
  if(!inode_is_root_user()){
    iunlockput(ip);
    end_op();
    return -1;
  }
  ops = vfs_dev_vops(ip->dev);
  if(ops && ops->setattr){
    if(ops->setattr(ip, 0, 0, uid >= 0, uid, gid >= 0, gid) < 0){
      iunlockput(ip);
      end_op();
      return -1;
    }
  } else {
    if(uid >= 0)
      ip->uid = uid;
    if(gid >= 0)
      ip->gid = gid;
    iupdate(ip);
  }
  iunlockput(ip);
  end_op();
  return 0;
}

int
sys_mountinfo(void)
{
  struct vfs_mount_info *out;
  int max;

  if(argint(1, &max) < 0)
    return -1;
  if(max <= 0)
    return -1;
  if(argptr(0, (char**)&out, max * sizeof(*out)) < 0)
    return -1;

  return vfs_get_mounts(out, max);
}

int
sys_devblocks(void)
{
  int dev;

  if(argint(0, &dev) < 0)
    return -1;
  if(dev < 0 || dev >= NDEV)
    return -1;

  return bdev_nblocks((uint)dev);
}

int
sys_ext2fail(void)
{
  int which;
  int value;

  if(argint(0, &which) < 0 || argint(1, &value) < 0)
    return -1;

  return vfs_dev_faultctl(EXT2DEV, which, value);
}

int
sys_fsfault(void)
{
  int dev;
  int which;
  int value;

  if(argint(0, &dev) < 0 || argint(1, &which) < 0 || argint(2, &value) < 0)
    return -1;
  if(dev < 0 || dev >= NDEV)
    return -1;

  return vfs_dev_faultctl((uint)dev, which, value);
}

int
sys_mount(void)
{
  char *path, *fstype;
  int flags;
  int mount_flags;
  int has_dev_override;
  int dev_override;
  int dev;
  struct vfs *fs;
  char path_buf[256];
  char fstype_buf[256];

  if(argstr(0, &path) < 0)
    return -1;
  if(argstr(1, &fstype) < 0)
    return -1;
  if(argint(2, &flags) < 0)
    return -1;

  // Copy strings to kernel space
  safestrcpy(path_buf, path, sizeof(path_buf));
  safestrcpy(fstype_buf, fstype, sizeof(fstype_buf));

  has_dev_override = MNT_HASDEV(flags);
  dev_override = -1;
  if(has_dev_override)
    dev_override = MNT_GETDEV(flags);
  mount_flags = flags & ~MNT_DEVMASK;
  if(has_dev_override && (dev_override < 0 || dev_override >= NDEV))
    return -1;

  MOUNTDBG("sys_mount: path=%s type=%s flags=%x devovr=%d\n",
           path_buf, fstype_buf, mount_flags, dev_override);

  // Allocate and initialize filesystem backend based on type
  fs = (struct vfs*)kalloc();
  if(fs == 0)
    return -1;

  memset(fs, 0, sizeof(*fs));

  // Check fstype by comparing byte-by-byte (no strcmp in kernel)
  if(memcmp(fstype_buf, "procfs", 7) == 0) {
    vfs_procfs_init(fs);
  } else if(memcmp(fstype_buf, "xv6fs", 6) == 0) {
    vfs_xv6fs_init(fs);
  } else if(memcmp(fstype_buf, "ext2", 5) == 0) {
    vfs_ext2_init(fs);
  } else if(memcmp(fstype_buf, "msdosfs", 8) == 0 ||
            memcmp(fstype_buf, "fat", 4) == 0) {
    vfs_msdosfs_init(fs);
  } else {
    kfree((void*)fs);
    return -1;
  }

  dev = 255;
  if(memcmp(fstype_buf, "procfs", 7) == 0)
    dev = PROCFSDEV;
  else if(memcmp(fstype_buf, "ext2", 5) == 0)
    dev = has_dev_override ? dev_override : EXT2DEV;
  else if(memcmp(fstype_buf, "xv6fs", 6) == 0)
    dev = has_dev_override ? dev_override : ROOTDEV;
  else if(memcmp(fstype_buf, "msdosfs", 8) == 0 ||
          memcmp(fstype_buf, "fat", 4) == 0)
    dev = has_dev_override ? dev_override : DISK_DEV(3);

  if(vfs_register_mount(fs, dev, mount_flags, path_buf) < 0){
    MOUNTDBG("sys_mount: failed path=%s type=%s dev=%d\n", path_buf, fstype_buf, dev);
    return -1;
  }

  MOUNTDBG("sys_mount: ok path=%s type=%s dev=%d\n", path_buf, fstype_buf, dev);
  return 0;
}

int
sys_umount(void)
{
  char *path;
  char path_buf[256];

  if(argstr(0, &path) < 0)
    return -1;

  // Copy path to kernel space
  safestrcpy(path_buf, path, sizeof(path_buf));

  return vfs_unmount(path_buf);
}

int
sys_exec(void)
{
  char *path, *argv[MAXARG];
  int i;
  uint uargv, uarg;

  if(argstr(0, &path) < 0 || argint(1, (int*)&uargv) < 0){
    return -1;
  }
  memset(argv, 0, sizeof(argv));
  for(i=0;; i++){
    if(i >= NELEM(argv))
      return -1;
    if(fetchint(uargv+4*i, (int*)&uarg) < 0)
      return -1;
    if(uarg == 0){
      argv[i] = 0;
      break;
    }
    if(fetchstr(uarg, &argv[i]) < 0)
      return -1;
  }
  return exec(path, argv);
}

int
sys_pipe(void)
{
  int *fd;
  struct file *rf, *wf;
  int fd0, fd1;

  if(argptr(0, (void*)&fd, 2*sizeof(fd[0])) < 0)
    return -1;
  if(pipealloc(&rf, &wf) < 0)
    return -1;
  fd0 = -1;
  if((fd0 = fdalloc(rf)) < 0 || (fd1 = fdalloc(wf)) < 0){
    if(fd0 >= 0)
      myproc()->ofile[fd0] = 0;
    fileclose(rf);
    fileclose(wf);
    return -1;
  }
  fd[0] = fd0;
  fd[1] = fd1;
  return 0;
}

// lseek - reposition read/write file offset
// Returns the new offset on success, -1 on failure.
int
sys_lseek(void)
{
  struct file *f;
  int offset;
  int whence;
  int newoff;

  if(argfd(0, 0, &f) < 0 || argint(1, &offset) < 0 || argint(2, &whence) < 0)
    return -1;

  // Cannot seek on pipes or sockets
  if(f->type == FD_PIPE || f->type == FD_SOCKET)
    return -1;

  if(f->type != FD_INODE)
    return -1;

  ilock(f->ip);

  switch(whence){
  case 0: // SEEK_SET
    newoff = offset;
    break;
  case 1: // SEEK_CUR
    newoff = f->off + offset;
    break;
  case 2: // SEEK_END
    newoff = f->ip->size + offset;
    break;
  default:
    iunlock(f->ip);
    return -1;
  }

  // Check for negative offset
  if(newoff < 0){
    iunlock(f->ip);
    return -1;
  }

  f->off = newoff;
  iunlock(f->ip);
  return newoff;
}

// dup2 - duplicate a file descriptor to a specific fd number
// Returns newfd on success, -1 on failure.
int
sys_dup2(void)
{
  struct file *f;
  int oldfd, newfd;
  struct proc *curproc = myproc();

  if(argfd(0, &oldfd, &f) < 0 || argint(1, &newfd) < 0)
    return -1;

  // Validate newfd
  if(newfd < 0 || newfd >= NOFILE)
    return -1;

  // If oldfd == newfd, just return newfd (no-op per POSIX)
  if(oldfd == newfd)
    return newfd;

  // If newfd is already open, close it first
  if(curproc->ofile[newfd] != 0){
    fileclose(curproc->ofile[newfd]);
    curproc->ofile[newfd] = 0;
  }

  // Duplicate the file reference
  curproc->ofile[newfd] = f;
  filedup(f);

  return newfd;
}

// fcntl - file control
// Implements F_DUPFD, F_GETFD, F_SETFD, F_GETFL, F_SETFL
int
sys_fcntl(void)
{
  struct file *f;
  int fd;
  int cmd;
  int arg;
  int flags;
  struct proc *curproc = myproc();

  if(argfd(0, &fd, &f) < 0 || argint(1, &cmd) < 0)
    return -1;

  switch(cmd){
  case 0: // F_DUPFD - duplicate fd to lowest available >= arg
    if(argint(2, &arg) < 0)
      return -1;
    if(arg < 0 || arg >= NOFILE)
      return -1;
    // Find lowest available fd >= arg
    for(int i = arg; i < NOFILE; i++){
      if(curproc->ofile[i] == 0){
        curproc->ofile[i] = f;
        filedup(f);
        return i;
      }
    }
    return -1; // No available fd

  case 1: // F_GETFD - get file descriptor flags
    // We don't currently track per-fd flags (like FD_CLOEXEC)
    // Return 0 for now (no flags set)
    return 0;

  case 2: // F_SETFD - set file descriptor flags
    if(argint(2, &arg) < 0)
      return -1;
    // We don't currently implement FD_CLOEXEC, but accept the call
    // TODO: implement close-on-exec properly when exec is enhanced
    return 0;

  case 3: // F_GETFL - get file status flags
    flags = 0;
    if(f->readable && f->writable)
      flags = O_RDWR;
    else if(f->writable)
      flags = O_WRONLY;
    else
      flags = O_RDONLY;
    // Note: O_APPEND status isn't tracked per-file currently
    return flags;

  case 4: // F_SETFL - set file status flags
    if(argint(2, &arg) < 0)
      return -1;
    // Only O_APPEND and O_NONBLOCK are typically settable
    // We don't support O_NONBLOCK currently
    // O_APPEND could be supported but requires file struct changes
    // For now, accept the call silently
    return 0;

  case 1030: // F_DUPFD_CLOEXEC - duplicate with close-on-exec
    if(argint(2, &arg) < 0)
      return -1;
    if(arg < 0 || arg >= NOFILE)
      return -1;
    for(int i = arg; i < NOFILE; i++){
      if(curproc->ofile[i] == 0){
        curproc->ofile[i] = f;
        filedup(f);
        // TODO: set FD_CLOEXEC flag when implemented
        return i;
      }
    }
    return -1;

  default:
    return -1;
  }
}
