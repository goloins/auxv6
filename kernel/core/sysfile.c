//
// File-system system calls.
// Mostly argument checking, since we don't trust
// user code, and calls into file.c and fs.c.
//

#include "types.h"
#include "x86.h"
#include "defs.h"
#include "param.h"
#include "stat.h"
#include "mmu.h"
#include "memlayout.h"
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

#ifndef NSEC_PER_SEC
#define NSEC_PER_SEC 1000000000L
#endif

#ifndef UTIME_NOW
#define UTIME_NOW  ((long)1073741823L)
#endif

#ifndef UTIME_OMIT
#define UTIME_OMIT ((long)1073741822L)
#endif

struct aux_utimenspec {
  long tv_sec;
  long tv_nsec;
};

static int create_default_mode(short type);
static int create_device_mode(int mode);
static int inode_dir_read(struct inode *dp, struct dirent *de, uint64_t off);
static struct inode* inode_dir_lookup(struct inode *dp, char *name, uint *poff);
static int child_name_in_parent(struct inode *parent, uint child_inum, char *name);
static ushort visible_dirent_inum(uint inum);
static int buildcwd(struct inode *cwd, char *buf, int size);
static int inode_is_owner_or_root(struct inode *ip);
static int inode_is_root_user(void);
static struct inode* vfs_resolve(char *path);
static struct inode* vfs_resolve_parent(char *path, char *name);
static int remove_path(char *path, int dironly);
static int tmpfs_alloc_dev(void);
static int nfs_alloc_dev(void);
static int copyinstr_user(uint uaddr, char *dst, int dstsz);
// Phase 1A: fdtable functions
static int fdtable_expand(struct fdtable*);
static void fdtable_init(struct fdtable*);

static int
copyinstr_user(uint uaddr, char *dst, int dstsz)
{
  struct proc *p;
  pde_t *pgdir;
  int i;
  char c;

  if(dst == 0 || dstsz <= 0)
    return -1;

  p = myproc();
  pgdir = proc_pgdir(p);
  if(p == 0 || pgdir == 0)
    return -1;

  for(i = 0; i < dstsz; i++){
    if(copyin(pgdir, &c, uaddr + (uint)i, 1) < 0)
      return -1;
    dst[i] = c;
    if(c == 0)
      return 0;
  }

  dst[dstsz - 1] = 0;
  return -1;
}

int
proc_fd_limit(struct proc *p)
{
  if(p == 0)
    return NOFILE_HARD;
  if(p->rlimit_nofile_cur > (uint)NOFILE_HARD)
    return NOFILE_HARD;
  return (int)p->rlimit_nofile_cur;
}

//
// Phase 1A: File Descriptor Table (fdtable) Functions
// Replacement for legacy static global file-table model.
// Each process now has its own dynamic fdtable that grows on demand.
//

#define FDTABLE_INIT_CAPACITY 32  // Start small; grow exponentially

// Initialize a new fdtable with initial capacity.
static void
fdtable_init(struct fdtable *ft)
{
  ft->entries = (struct file**)kalloc();
  if(ft->entries == 0)
    panic("fdtable_init: kalloc entries failed");
  memset(ft->entries, 0, PGSIZE);
  ft->fdflags = (uint8_t*)kalloc();
  if(ft->fdflags == 0)
    panic("fdtable_init: kalloc fdflags failed");
  memset(ft->fdflags, 0, PGSIZE);
  ft->capacity = FDTABLE_INIT_CAPACITY;
  ft->nfds = 0;
  ft->next_fd_hint = 0;
}

// Expand fdtable by doubling capacity (or add 256 slots, whichever is larger).
// Returns 0 on success, -1 on failure.
static int
fdtable_expand(struct fdtable *ft)
{
  int new_capacity;
  uint new_bytes;
  struct file **new_entries;
  uint8_t *new_fdflags;

  new_capacity = (ft->capacity > 256) ? (ft->capacity * 2) : (ft->capacity + 256);

  new_bytes = (uint)new_capacity * sizeof(struct file *);
  if(new_bytes > PGSIZE)
    return -1;
  if((uint)new_capacity > PGSIZE)  // fdflags is 1 byte per slot
    return -1;

  new_entries = (struct file**)kalloc();
  if(new_entries == 0)
    return -1;
  new_fdflags = (uint8_t*)kalloc();
  if(new_fdflags == 0){
    kfree((char*)new_entries);
    return -1;
  }

  // Copy existing entries and flags
  memmove(new_entries, ft->entries, ft->capacity * sizeof(struct file *));
  memset(&new_entries[ft->capacity], 0,
         (new_capacity - ft->capacity) * sizeof(struct file *));
  memmove(new_fdflags, ft->fdflags, (uint)ft->capacity);
  memset(&new_fdflags[ft->capacity], 0, (uint)(new_capacity - ft->capacity));

  kfree((char*)ft->entries);
  kfree((char*)ft->fdflags);
  ft->entries  = new_entries;
  ft->fdflags  = new_fdflags;
  ft->capacity = new_capacity;
  return 0;
}

// Allocate a new fdtable for a process.
struct fdtable*
fdtable_alloc(void)
{
  struct fdtable *ft;

  ft = (struct fdtable*)kalloc();
  if(ft == 0)
    return 0;

  memset(ft, 0, PGSIZE);
  fdtable_init(ft);
  return ft;
}

// Free an fdtable and all its file references.
void
fdtable_free(struct fdtable *ft)
{
  int i;
  
  if(ft == 0)
    return;
  
  for(i = 0; i < ft->nfds; i++){
    if(ft->entries[i]){
      fileclose(ft->entries[i]);
      ft->entries[i] = 0;
    }
  }

  kfree((char*)ft->entries);
  kfree((char*)ft->fdflags);
  kfree((char*)ft);
}

// Duplicate fdtable from parent during fork.
// Returns 0 on success, -1 on failure.
int
fdtable_dup(struct fdtable *parent_ft, struct fdtable *child_ft)
{
  int i;

  if(parent_ft == 0)
    return -1;

  // Ensure destination capacity and clean inherited default entries.
  if(child_ft->entries == 0)
    return -1;
  if(parent_ft->capacity > child_ft->capacity){
    if(fdtable_expand(child_ft) < 0)
      return -1;
  }

  memset(child_ft->entries, 0, child_ft->capacity * sizeof(struct file *));
  memset(child_ft->fdflags, 0, child_ft->capacity);
  child_ft->capacity = parent_ft->capacity;
  child_ft->nfds = parent_ft->nfds;
  child_ft->next_fd_hint = parent_ft->next_fd_hint;

  // Duplicate file references and copy flags (FD_CLOEXEC survives fork; exec clears)
  for(i = 0; i < parent_ft->nfds; i++){
    if(parent_ft->entries[i]){
      child_ft->entries[i] = filedup(parent_ft->entries[i]);
      child_ft->fdflags[i] = parent_ft->fdflags[i];
    }
  }

  return 0;
}

// Wrapper for fdtable_expand exported to defs.h
int
fdtable_grow(struct fdtable *ft)
{
  return fdtable_expand(ft);
}

//
// Phase 1A: File descriptor access helpers
// These abstract the fdtable implementation from syscall code.
//

// Get file from current process's fdtable by fd number.
// Returns NULL if fd is invalid or not open.
static inline struct file*
fd_get(int fd)
{
  struct proc *curproc = myproc();
  
  if(curproc == 0 || curproc->fdtable == 0 || fd < 0 || fd >= curproc->fdtable->nfds)
    return 0;
  
  return curproc->fdtable->entries[fd];
}

// Set file in current process's fdtable at fd.
// Assumes fd is valid (0 <= fd < nfds).
static inline void
fd_put(int fd, struct file *f)
{
  struct proc *curproc = myproc();
  
  if(curproc && curproc->fdtable && fd >= 0 && fd < curproc->fdtable->nfds)
    curproc->fdtable->entries[fd] = f;
}

// Clear file in current process's fdtable at fd.
static inline void
fd_clear(int fd)
{
  struct proc *curproc = myproc();

  if(curproc == 0 || curproc->fdtable == 0 || fd < 0 || fd >= curproc->fdtable->nfds)
    return;

  curproc->fdtable->entries[fd] = 0;
  curproc->fdtable->fdflags[fd] = 0;
  if(fd < curproc->fdtable->next_fd_hint)
    curproc->fdtable->next_fd_hint = fd;

  // Keep nfds as a high-water mark without trailing holes.
  while(curproc->fdtable->nfds > 0 &&
        curproc->fdtable->entries[curproc->fdtable->nfds - 1] == 0)
    curproc->fdtable->nfds--;
}

// Ensure current process fdtable can address slot 'fd'.
// Returns 0 on success, -1 on failure.
static int
fd_ensure_slot(struct proc *p, int fd)
{
  if(p == 0 || p->fdtable == 0 || fd < 0)
    return -1;

  while(fd >= p->fdtable->capacity){
    if(fdtable_expand(p->fdtable) < 0)
      return -1;
  }

  if(fd >= p->fdtable->nfds)
    p->fdtable->nfds = fd + 1;

  return 0;
}

#define SELECT_BITS_PER_WORD (8 * sizeof(uint))
/* Must match userland fd_set capacity in include/sys/types.h. */
#define SELECT_FD_SETSIZE 64

#define POLLIN   0x0001
#define POLLPRI  0x0002
#define POLLOUT  0x0004
#define POLLERR  0x0008
#define POLLHUP  0x0010
#define POLLNVAL 0x0020

struct ktimeval {
  int tv_sec;
  int tv_usec;
};

struct kpollfd {
  int fd;
  short events;
  short revents;
};

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
inode_dir_read(struct inode *dp, struct dirent *de, uint64_t off)
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

static ushort
visible_dirent_inum(uint inum)
{
  ushort vinum;

  vinum = (ushort)(inum & 0xFFFF);
  if(vinum == 0)
    vinum = 1;
  return vinum;
}

static int
child_name_in_parent(struct inode *parent, uint child_inum, char *name)
{
  uint64_t off;
  int i;
  struct dirent de;
  ushort want_inum;

  want_inum = visible_dirent_inum(child_inum);

  for(off = 0; ; off += sizeof(de)){
    int r;

    r = inode_dir_read(parent, &de, off);
    if(r == 0)
      break;
    if(r != sizeof(de))
      return -1;
    if(de.inum != want_inum)
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
  char part[DIRSIZ + 1];
  int depth;
  int plen;
  char *dst;

  if(cwd == 0 || buf == 0 || size < 2)
    return -1;

  ip = idup(cwd);
  depth = 0;
  dst = buf + size - 1;
  *dst = 0;

  for(;;){
    ilock(ip);
    if(vfs_is_system_root_inode(ip)){
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
       child_name_in_parent(parent, ip->inum, part) < 0){
      iunlockput(parent);
      iput(ip);
      return -1;
    }
    plen = strlen(part);
    if(plen > 0){
      if(dst - buf < plen + 1){
        iunlockput(parent);
        iput(ip);
        return -1;
      }
      dst -= plen;
      memmove(dst, part, plen);
      *--dst = '/';
    }
    depth++;
    iunlock(parent);
    iput(ip);
    ip = parent;
  }
  iput(ip);

  if(*dst == 0){
    buf[0] = '/';
    buf[1] = 0;
    return 0;
  }

  memmove(buf, dst, (buf + size) - dst);
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

  if(vfs_lookup_follow(path, &vn) < 0)
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
// Phase 1A: Updated to work with dynamic fdtable instead of fixed ofile[].
static int
argfd(int n, int *pfd, struct file **pf)
{
  int fd;
  struct file *f;
  struct proc *curproc = myproc();

  if(argint(n, &fd) < 0)
    return -1;
  
  if(curproc == 0 || curproc->fdtable == 0)
    return -1;
  
  // Bounds check: fd must be valid and exist in fdtable
  if(fd < 0 || fd >= curproc->fdtable->nfds || (f = curproc->fdtable->entries[fd]) == 0)
    return -1;

  // Defensive hardening: reject obviously corrupt file pointers early.
  if((uint)f < KERNBASE)
    return -1;
  if(f->magic != FILE_MAGIC || f->ref < 1)
    return -1;
  
  if(pfd)
    *pfd = fd;
  if(pf)
    *pf = f;
  return 0;
}

// Allocate a file descriptor for the given file.
// Takes over file reference from caller on success.
// Phase 1A: Uses dynamic fdtable instead of global ftable array.
static int
fdalloc(struct file *f)
{
  int fd;
  int start;
  int limit_scan;
  struct proc *curproc = myproc();
  int limit;

  if(curproc == 0 || curproc->fdtable == 0)
    return -1;

  limit = proc_fd_limit(curproc);

  // Ensure capacity can represent the process limit.
  while(curproc->fdtable->capacity < limit){
    if(fdtable_expand(curproc->fdtable) < 0)
      return -1;
  }

  start = curproc->fdtable->next_fd_hint;
  if(start < 0 || start >= limit)
    start = 0;
  if(start >= curproc->fdtable->nfds)
    start = 0;

  // Prefer the hint region first, then wrap to preserve low-fd reuse.
  for(fd = start; fd < limit; fd++){
    if(fd >= curproc->fdtable->nfds)
      curproc->fdtable->nfds = fd + 1;
    if(curproc->fdtable->entries[fd] == 0){
      curproc->fdtable->entries[fd] = f;
      curproc->fdtable->next_fd_hint = fd + 1;
      if(curproc->fdtable->next_fd_hint >= limit)
        curproc->fdtable->next_fd_hint = 0;
      return fd;
    }
  }

  limit_scan = (start < limit) ? start : limit;
  for(fd = 0; fd < limit_scan; fd++){
    if(fd >= curproc->fdtable->nfds)
      curproc->fdtable->nfds = fd + 1;
    if(curproc->fdtable->entries[fd] == 0){
      curproc->fdtable->entries[fd] = f;
      curproc->fdtable->next_fd_hint = fd + 1;
      if(curproc->fdtable->next_fd_hint >= limit)
        curproc->fdtable->next_fd_hint = 0;
      return fd;
    }
  }

  return -1;
}

static int
fd_ready_events(struct file *f)
{
  int rd;
  int wr;
  int err;
  int events;

  if(f == 0)
    return POLLNVAL;

  events = 0;
  switch(f->type){
  case FD_INODE:
    if(f->ip && f->ip->type == T_DEV && f->ip->major == AUDIODEV){
      rd = 0;
      wr = 0;
      err = 0;
      audio_poll_events(f, &rd, &wr, &err);
      if(f->readable && rd)
        events |= POLLIN;
      if(f->writable && wr)
        events |= POLLOUT;
      if(err)
        events |= POLLERR | POLLHUP;
      break;
    }
    if(f->ip && f->ip->type == T_DEV && f->ip->major == PTYDEV){
      rd = 0;
      wr = 0;
      err = 0;
      pty_poll_events(f, &rd, &wr, &err);
      if(f->readable && rd)
        events |= POLLIN;
      if(f->writable && wr)
        events |= POLLOUT;
      if(err)
        events |= POLLERR | POLLHUP;
      break;
    }
    if(f->ip && f->ip->type == T_DEV && f->ip->major == TUNTAPDEV){
      rd = 0;
      wr = 0;
      err = 0;
      tuntap_poll_events(f, &rd, &wr, &err);
      if(f->readable && rd)
        events |= POLLIN;
      if(f->writable && wr)
        events |= POLLOUT;
      if(err)
        events |= POLLERR | POLLHUP;
      break;
    }
    if(f->ip && f->ip->type == T_DEV && f->ip->major == CONSOLE &&
       f->ip->minor == CONSOLE_MINOR_MOUSE0){
      rd = 0;
      wr = 0;
      err = 0;
      console_mouse_poll_events(&rd, &wr, &err);
      if(f->readable && rd)
        events |= POLLIN;
      if(err)
        events |= POLLERR | POLLHUP;
      break;
    }
    if(f->ip && f->ip->type == T_DEV && f->ip->major == CONSOLE &&
       f->ip->minor == CONSOLE_MINOR_KBD0){
      rd = 0;
      wr = 0;
      err = 0;
      console_kbd_poll_events(&rd, &wr, &err);
      if(f->readable && rd)
        events |= POLLIN;
      if(err)
        events |= POLLERR | POLLHUP;
      break;
    }
    if(f->readable)
      events |= POLLIN;
    if(f->writable)
      events |= POLLOUT;
    break;
  case FD_PIPE:
    rd = pipe_readable(f->pipe);
    wr = pipe_writable(f->pipe);
    if(f->readable && rd)
      events |= POLLIN;
    if(f->writable && wr)
      events |= POLLOUT;
    if(f->readable && !pipe_writeopen(f->pipe))
      events |= POLLHUP;
    break;
  case FD_SOCKET:
    rd = 0;
    wr = 0;
    err = 0;
    socket_poll_events(f->socket, &rd, &wr, &err);
    if(f->readable && rd)
      events |= POLLIN;
    if(f->writable && wr)
      events |= POLLOUT;
    if(err)
      events |= POLLERR;
    break;
  default:
    events |= POLLNVAL;
    break;
  }

  return events;
}

static int
poll_scan(struct kpollfd *fds, int nfds)
{
  int i;
  int ready;
  int revents;
  int mask;
  int fd;
  struct proc *p;
  struct file *f;

  ready = 0;
  p = myproc();

  for(i = 0; i < nfds; i++){
    revents = 0;
    fd = fds[i].fd;

    if(fd < 0){
      fds[i].revents = 0;
      continue;
    }

    if(p == 0 || p->fdtable == 0 || fd >= p->fdtable->nfds || p->fdtable->entries[fd] == 0){
      revents = POLLNVAL;
    } else {
      f = p->fdtable->entries[fd];
      mask = fd_ready_events(f);
      revents = mask & (fds[i].events | POLLERR | POLLHUP | POLLNVAL);
    }

    fds[i].revents = (short)revents;
    if(revents)
      ready++;
  }

  return ready;
}

static int
select_set_has(int *set, int fd)
{
  int word;
  int bit;

  word = fd / SELECT_BITS_PER_WORD;
  bit = fd % SELECT_BITS_PER_WORD;
  return (set[word] & (1U << bit)) != 0;
}

static void
select_set_add(int *set, int fd)
{
  int word;
  int bit;

  word = fd / SELECT_BITS_PER_WORD;
  bit = fd % SELECT_BITS_PER_WORD;
  set[word] |= (1U << bit);
}

static int
select_scan(int nfds, int *in_read, int *in_write, int *in_except,
            int *out_read, int *out_write, int *out_except,
            int words)
{
  int fd;
  int ready;
  int mask;
  struct proc *p;
  struct file *f;

  ready = 0;
  p = myproc();

  memset(out_read, 0, words * sizeof(int));
  memset(out_write, 0, words * sizeof(int));
  memset(out_except, 0, words * sizeof(int));

  for(fd = 0; fd < nfds; fd++){
    if((!in_read || !select_set_has(in_read, fd)) &&
       (!in_write || !select_set_has(in_write, fd)) &&
       (!in_except || !select_set_has(in_except, fd)))
      continue;

    if(p == 0 || p->fdtable == 0 || fd >= p->fdtable->nfds || p->fdtable->entries[fd] == 0){
      if(in_except && select_set_has(in_except, fd)){
        select_set_add(out_except, fd);
        ready++;
      }
      continue;
    }

    f = p->fdtable->entries[fd];
    mask = fd_ready_events(f);

    if(in_read && select_set_has(in_read, fd) && (mask & (POLLIN | POLLHUP | POLLERR))){
      select_set_add(out_read, fd);
      ready++;
    }
    if(in_write && select_set_has(in_write, fd) && (mask & (POLLOUT | POLLERR))){
      select_set_add(out_write, fd);
      ready++;
    }
    if(in_except && select_set_has(in_except, fd) && (mask & (POLLERR | POLLNVAL))){
      select_set_add(out_except, fd);
      ready++;
    }
  }

  return ready;
}

static int
timeout_ms_to_ticks(int timeout_ms)
{
  if(timeout_ms <= 0)
    return 0;
  return (timeout_ms + 9) / 10;
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
  struct proc *p;
  pde_t *pgdir;
  int n;
  int addr;
  uint uaddr;
  char *kbuf;
  int tot;
  int want;
  int r;

  if(argfd(0, 0, &f) < 0 || argint(1, &addr) < 0 || argint(2, &n) < 0)
    return -1;
  if(n < 0)
    return -1;
  if(n == 0)
    return 0;

  p = myproc();
  pgdir = proc_pgdir(p);
  if(p == 0 || pgdir == 0)
    return -1;

  uaddr = (uint)addr;
  kbuf = (char*)kmalloc(PGSIZE);
  if(kbuf == 0)
    return -1;

  r = -1;
  tot = 0;
  while(tot < n){
    want = n - tot;
    if(want > PGSIZE)
      want = PGSIZE;

    r = fileread(f, kbuf, want);
    if(r <= 0)
      break;

    if(copyout(pgdir, uaddr + (uint)tot, kbuf, (uint)r) < 0){
      kmalloc_free(kbuf);
      return (tot > 0) ? tot : -1;
    }

    tot += r;
    if(r < want)
      break;
  }

  kmalloc_free(kbuf);
  if(tot > 0)
    return tot;
  return r;
}

int
sys_getdents(void)
{
  struct file *f;
  int uents_addr;
  uint uents_u;
  struct dirent *kents;
  struct proc *p;
  struct dirent de;
  int max;
  int out;
  int r;
  pde_t *pgdir;

  if(argfd(0, 0, &f) < 0 || argint(2, &max) < 0)
    return -1;
  if(max < 0)
    return -1;
  if(max > PGSIZE / sizeof(struct dirent))
    return -1;
  if(argint(1, &uents_addr) < 0)
    return -1;
  uents_u = (uint)uents_addr;

  p = myproc();
  pgdir = proc_pgdir(p);
  if(p == 0 || pgdir == 0)
    return -1;

  if(max == 0)
    return 0;

  if(f->type != FD_INODE)
    return -1;

  ilock(f->ip);
  if(f->ip->type != T_DIR){
    iunlock(f->ip);
    return -1;
  }
  iunlock(f->ip);

  kents = (struct dirent*)kmalloc(max * sizeof(*kents));
  if(kents == 0)
    return -1;

  out = 0;
  while(out < max){
    r = fileread(f, (char*)&de, sizeof(de));
    if(r == 0)
      break;
    if(r < 0){
      kmalloc_free(kents);
      return (out > 0) ? out : -1;
    }
    if(r != sizeof(de))
      break;
    if(de.inum == 0)
      continue;
    if(!vfs_dirent_visible(f->ip, &de))
      continue;
    kents[out++] = de;
  }

  if(out > 0 && copyout(pgdir, uents_u, kents, out * sizeof(*kents)) < 0){
    kmalloc_free(kents);
    return -1;
  }

  kmalloc_free(kents);

  return out;
}

int
sys_write(void)
{
  struct file *f;
  struct proc *p;
  pde_t *pgdir;
  int n;
  int addr;
  uint uaddr;
  char *kbuf;
  char kbuf_small[512];
  int use_heap;
  int tot;
  int want;
  int r;

  if(argfd(0, 0, &f) < 0 || argint(1, &addr) < 0 || argint(2, &n) < 0)
    return -1;
  if(n < 0)
    return -1;
  if(n == 0)
    return 0;

  p = myproc();
  pgdir = proc_pgdir(p);
  if(p == 0 || pgdir == 0)
    return -1;

  uaddr = (uint)addr;
  use_heap = (n > (int)sizeof(kbuf_small));
  if(use_heap){
    kbuf = (char*)kmalloc(PGSIZE);
    if(kbuf == 0)
      return -1;
  } else {
    kbuf = kbuf_small;
  }

  r = -1;
  tot = 0;
  while(tot < n){
    want = n - tot;
    if(want > (use_heap ? PGSIZE : (int)sizeof(kbuf_small)))
      want = use_heap ? PGSIZE : (int)sizeof(kbuf_small);

    if(copyin(pgdir, kbuf, uaddr + (uint)tot, (uint)want) < 0){
      if(use_heap)
        kmalloc_free(kbuf);
      return (tot > 0) ? tot : -1;
    }

    r = filewrite(f, kbuf, want);
    if(r <= 0)
      break;

    tot += r;
    if(r < want)
      break;
  }

  if(use_heap)
    kmalloc_free(kbuf);
  if(tot > 0)
    return tot;
  return r;
}

int
sys_close(void)
{
  int fd;
  struct file *f;

  if(argfd(0, &fd, &f) < 0)
    return -1;
  fd_clear(fd);
  fileclose(f);
  return 0;
}

int
sys_fstat(void)
{
  struct file *f;
  struct stat st;
  int staddr;
  struct proc *p;
  pde_t *pgdir;

  if(argfd(0, 0, &f) < 0 || argint(1, &staddr) < 0)
    return -1;
  if(filestat(f, &st) < 0)
    return -1;
  p = myproc();
  pgdir = proc_pgdir(p);
  if(p == 0 || pgdir == 0)
    return -1;
  if(copyout(pgdir, (uint)staddr, &st, sizeof(st)) < 0)
    return -1;
  return 0;
}

// stat by path — does not require read permission on the target itself,
// only execute permission on each directory component in the path.
int
sys_stat(void)
{
  int path_addr;
  char path[256];
  const struct vnode_ops *ops;
  int rc;
  struct stat st;
  int staddr;
  struct proc *p;
  pde_t *pgdir;
  struct inode *ip;

  if(argint(0, &path_addr) < 0 || argint(1, &staddr) < 0)
    return -1;
  if(copyinstr_user((uint)path_addr, path, sizeof(path)) < 0)
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
    rc = ops->stat(ip, &st);
  } else {
    rc = 0;
    stati(ip, &st);
  }
  iunlockput(ip);
  end_op();
  if(rc < 0)
    return rc;

  p = myproc();
  pgdir = proc_pgdir(p);
  if(p == 0 || pgdir == 0)
    return -1;
  if(copyout(pgdir, (uint)staddr, &st, sizeof(st)) < 0)
    return -1;
  return 0;
}

// Create the path new as a link to the same inode as old.
int
sys_link(void)
{
  int old_addr, new_addr;
  const struct vnode_ops *ops;
  char name[DIRSIZ], old[256], new[256];
  struct inode *dp, *ip;

  if(argint(0, &old_addr) < 0 || argint(1, &new_addr) < 0)
    return -1;
  if(copyinstr_user((uint)old_addr, old, sizeof(old)) < 0 ||
     copyinstr_user((uint)new_addr, new, sizeof(new)) < 0)
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
  int old_addr, new_addr;
  const struct vnode_ops *ops;
  char oldname[DIRSIZ], newname[DIRSIZ], old[256], new[256];
  struct inode *olddp, *newdp;
  struct inode *first, *second;
  int rc;

  if(argint(0, &old_addr) < 0 || argint(1, &new_addr) < 0)
    return -1;
  if(copyinstr_user((uint)old_addr, old, sizeof(old)) < 0 ||
     copyinstr_user((uint)new_addr, new, sizeof(new)) < 0)
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
  uint64_t off;
  struct dirent de;

  for(off=2*sizeof(de); off<dp->size; off+=sizeof(de)){
    if(inode_dir_read(dp, &de, off) != sizeof(de))
      panic("isdirempty: readi");
    if(de.inum != 0)
      return 0;
  }
  return 1;
}

static int
remove_path(char *path, int dironly)
{
  const struct vnode_ops *ops;
  struct inode *ip, *dp;
  struct dirent de;
  char name[DIRSIZ];
  uint off;

  begin_op();
  if((dp = vfs_resolve_parent(path, name)) == 0){
    end_op();
    return -1;
  }

  ilock(dp);
  if(iaccess(dp, IACC_WRITE | IACC_EXEC) < 0)
    goto bad_dp;

  if(namecmp(name, ".") == 0 || namecmp(name, "..") == 0)
    goto bad_dp;

  if((ip = inode_dir_lookup(dp, name, &off)) == 0)
    goto bad_dp;
  ilock(ip);

  if(ip->nlink < 1)
    panic("remove_path: nlink < 1");

  if(dironly) {
    if(ip->type != T_DIR){
      iunlockput(ip);
      goto bad_dp;
    }
    if(!isdirempty(ip)){
      iunlockput(ip);
      goto bad_dp;
    }
  } else {
    if(ip->type == T_DIR){
      iunlockput(ip);
      goto bad_dp;
    }
  }

  ops = vfs_dev_vops(dp->dev);
  if(ops && ops->remove){
    iunlockput(ip);
    if(ops->remove(dp, name) < 0)
      goto bad_dp;
    iunlockput(dp);
    end_op();
    return 0;
  }

  memset(&de, 0, sizeof(de));
  if(writei(dp, (char*)&de, off, sizeof(de)) != sizeof(de))
    panic("remove_path: writei");
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

bad_dp:
  iunlockput(dp);
  end_op();
  return -1;
}

//PAGEBREAK!
int
sys_unlink(void)
{
  int path_addr;
  char path[256];

  if(argint(0, &path_addr) < 0)
    return -1;
  if(copyinstr_user((uint)path_addr, path, sizeof(path)) < 0)
    return -1;
  return remove_path(path, 0);
}

int
sys_rmdir(void)
{
  int path_addr;
  char path[256];

  if(argint(0, &path_addr) < 0)
    return -1;
  if(copyinstr_user((uint)path_addr, path, sizeof(path)) < 0)
    return -1;
  return remove_path(path, 1);
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
  int path_addr;
  char path[256];
  int fd, omode;
  int cmode;
  int must_write;
  uint64_t startoff;  /* O_APPEND: set to ip->size; otherwise 0 */
  struct file *f;
  struct inode *ip;
  const struct vnode_ops *ops;

  if(argint(0, &path_addr) < 0 || argint(1, &omode) < 0)
    return -1;
  cmode = 0;
  if(omode & O_CREATE){
    if(argint(2, &cmode) < 0)
      cmode = 0;
    cmode &= 0777;
    cmode &= ~myproc()->umask;
  }
  if(copyinstr_user((uint)path_addr, path, sizeof(path)) < 0)
    return -1;

  begin_op();

  if(omode & O_CREATE){
    // O_CREATE should still open an existing vnode when present.
    ip = vfs_resolve(path);
    if(ip == 0){
      ip = create(path, T_FILE, 0, 0, cmode);
      if(ip == 0){
        end_op();
        return -1;
      }
    } else {
      ilock(ip);
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

  if(ip->type == T_DEV && ip->major == PTYDEV){
    if(pty_open(f, ip->minor) < 0){
      fd_clear(fd);
      fileclose(f);
      iunlockput(ip);
      end_op();
      return -1;
    }
  }

  must_write = (omode & O_WRONLY) || ((omode & O_RDWR) == O_RDWR);
  if((omode & O_TRUNC) && must_write){
    ops = vfs_dev_vops(ip->dev);
    if(ops && ops->truncate){
      if(ops->truncate(ip) < 0){
        fd_clear(fd);
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

  if(ip->type == T_DEV && ip->major == SERIALDEV){
    if(serial_open(f, ip->minor, omode) < 0){
      fd_clear(fd);
      fileclose(f);
      return -1;
    }
  }

  if(ip->type == T_DEV && ip->major == AUDIODEV){
    if(audio_open(f, ip->minor, omode) < 0){
      fd_clear(fd);
      fileclose(f);
      return -1;
    }
  }

  if(ip->type == T_DEV && ip->major == TUNTAPDEV){
    if(tuntap_open(f, ip->minor, omode) < 0){
      fd_clear(fd);
      fileclose(f);
      return -1;
    }
  }

  return fd;
}

int
sys_mkdir(void)
{
  int path_addr;
  char path[256];
  struct inode *ip;

  if(argint(0, &path_addr) < 0)
    return -1;
  if(copyinstr_user((uint)path_addr, path, sizeof(path)) < 0)
    return -1;
  begin_op();
  if((ip = create(path, T_DIR, 0, 0, 0)) == 0){
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
  int path_addr;
  struct inode *ip;
  char path[256];
  int mode;
  int major, minor;

  if(argint(0, &path_addr) < 0 ||
      argint(1, &mode) < 0 ||
      argint(2, &major) < 0 ||
      argint(3, &minor) < 0)
    return -1;
  if(copyinstr_user((uint)path_addr, path, sizeof(path)) < 0)
    return -1;
  if(((mode & M_IFMT) != M_IFCHR && (mode & M_IFMT) != M_IFBLK))
    return -1;
  begin_op();
  if((ip = create(path, T_DEV, major, minor, mode)) == 0){
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
  int path_addr;
  char path[256];
  struct inode *ip;
  struct proc *curproc = myproc();
  
  if(argint(0, &path_addr) < 0)
    return -1;
  if(copyinstr_user((uint)path_addr, path, sizeof(path)) < 0)
    return -1;
  begin_op();
  if((ip = vfs_resolve(path)) == 0){
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
  int bufaddr;
  int size;
  char path[128];
  struct proc *p;

  if(argint(1, &size) < 0)
    return -1;
  if(size <= 1 || argint(0, &bufaddr) < 0)
    return -1;

  begin_op();
  if(buildcwd(myproc()->cwd, path, sizeof(path)) < 0){
    end_op();
    return -1;
  }
  end_op();

  if(strlen(path) + 1 > (uint)size)
    return -1;

  p = myproc();
  if(p == 0 || proc_pgdir(p) == 0)
    return -1;
  if(copyout(proc_pgdir(p), (uint)bufaddr, path, strlen(path) + 1) < 0)
    return -1;
  return 0;
}

int
sys_chmod(void)
{
  int path_addr;
  char path[256];
  int mode;
  struct inode *ip;
  const struct vnode_ops *ops;

  if(argint(0, &path_addr) < 0 || argint(1, &mode) < 0)
    return -1;
  if(copyinstr_user((uint)path_addr, path, sizeof(path)) < 0)
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
sys_fchmod(void)
{
  int fd;
  int mode;
  struct file *f;
  struct inode *ip;
  const struct vnode_ops *ops;

  if(argint(0, &fd) < 0 || argint(1, &mode) < 0)
    return -1;
  if(argfd(fd, 0, &f) < 0)
    return -1;
  if(f->type != FD_INODE || f->ip == 0)
    return -1;

  ip = f->ip;
  begin_op();
  ilock(ip);
  if(!inode_is_owner_or_root(ip)){
    iunlock(ip);
    end_op();
    return -1;
  }
  ops = vfs_dev_vops(ip->dev);
  if(ops && ops->setattr){
    if(ops->setattr(ip, 1, mode, 0, 0, 0, 0) < 0){
      iunlock(ip);
      end_op();
      return -1;
    }
  } else {
    ip->mode = (ip->mode & M_IFMT) | (mode & 07777);
    iupdate(ip);
  }
  iunlock(ip);
  end_op();
  return 0;
}

int
sys_chown(void)
{
  int path_addr;
  char path[256];
  int uid;
  int gid;
  struct inode *ip;
  const struct vnode_ops *ops;

  if(argint(0, &path_addr) < 0 || argint(1, &uid) < 0 || argint(2, &gid) < 0)
    return -1;
  if(copyinstr_user((uint)path_addr, path, sizeof(path)) < 0)
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
sys_utimensat(void)
{
  int dirfd;
  int path_addr;
  int times_addr;
  int flags;
  char path[256];
  struct aux_utimenspec times[2];
  struct inode *ip;
  const struct vnode_ops *ops;
  int set_atime;
  int set_mtime;
  uint atime_sec;
  uint mtime_sec;
  uint now_sec;

  if(argint(0, &dirfd) < 0 || argint(1, &path_addr) < 0 ||
     argint(2, &times_addr) < 0 || argint(3, &flags) < 0)
    return -1;

  if(dirfd != AT_FDCWD)
    return -1;
  if((flags & ~AT_SYMLINK_NOFOLLOW) != 0)
    return -1;
  if(copyinstr_user((uint)path_addr, path, sizeof(path)) < 0)
    return -1;

  set_atime = 1;
  set_mtime = 1;
  if(times_addr != 0){
    struct proc *p;

    p = myproc();
    if(p == 0 || proc_pgdir(p) == 0)
      return -1;
    if(copyin(proc_pgdir(p), (char*)times, (uint)times_addr, sizeof(times)) < 0)
      return -1;

    if((times[0].tv_nsec != UTIME_NOW && times[0].tv_nsec != UTIME_OMIT &&
        (times[0].tv_nsec < 0 || times[0].tv_nsec >= NSEC_PER_SEC)) ||
       (times[1].tv_nsec != UTIME_NOW && times[1].tv_nsec != UTIME_OMIT &&
        (times[1].tv_nsec < 0 || times[1].tv_nsec >= NSEC_PER_SEC)))
      return -1;

    set_atime = (times[0].tv_nsec != UTIME_OMIT);
    set_mtime = (times[1].tv_nsec != UTIME_OMIT);
  }

  if(!set_atime && !set_mtime)
    return 0;

  acquire(&tickslock);
  now_sec = ticks;
  release(&tickslock);
  atime_sec = now_sec;
  mtime_sec = now_sec;

  if(times_addr != 0){
    if(set_atime && times[0].tv_nsec != UTIME_NOW){
      if(times[0].tv_sec < 0)
        return -1;
      atime_sec = (uint)times[0].tv_sec;
    }
    if(set_mtime && times[1].tv_nsec != UTIME_NOW){
      if(times[1].tv_sec < 0)
        return -1;
      mtime_sec = (uint)times[1].tv_sec;
    }
  }

  begin_op();
  if(flags & AT_SYMLINK_NOFOLLOW)
    ip = vfs_namei(path);
  else
    ip = vfs_resolve(path);
  if(ip == 0){
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
  if(ops == 0 || ops->settimes == 0 ||
     ops->settimes(ip, set_atime, atime_sec, set_mtime, mtime_sec) < 0){
    iunlockput(ip);
    end_op();
    return -1;
  }

  iunlockput(ip);
  end_op();
  return 0;
}

int
sys_mountinfo(void)
{
  int out_addr;
  uint out_u;
  struct vfs_mount_info *kout;
  struct proc *p;
  int max;
  int n;

  if(argint(1, &max) < 0)
    return -1;
  if(max <= 0)
    return -1;
  if(max > VFS_MOUNTS_MAX)
    max = VFS_MOUNTS_MAX;
  if(argint(0, &out_addr) < 0)
    return -1;
  out_u = (uint)out_addr;

  kout = (struct vfs_mount_info*)kmalloc(max * sizeof(*kout));
  if(kout == 0)
    return -1;

  n = vfs_get_mounts(kout, max);
  if(n < 0){
    kmalloc_free(kout);
    return -1;
  }

  if(n > 0){
    p = myproc();
    if(p == 0 || proc_pgdir(p) == 0 ||
       copyout(proc_pgdir(p), out_u, kout, n * sizeof(*kout)) < 0){
      kmalloc_free(kout);
      return -1;
    }
  }

  kmalloc_free(kout);
  return n;
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
  int path_addr;
  int fstype_addr;
  int flags;
  int data_addr;
  uint data_u;
  char *data_buf;
  int datalen;
  int mount_flags;
  int has_dev_override;
  int dev_override;
  int dev;
  struct proc *p;
  pde_t *pgdir;
  struct vfs *fs;
  char path_buf[256];
  char fstype_buf[256];

  if(argint(0, &path_addr) < 0)
    return -1;
  if(argint(1, &fstype_addr) < 0)
    return -1;
  if(argint(2, &flags) < 0)
    return -1;
  if(argint(4, &datalen) < 0)
    return -1;
  if(datalen < 0)
    return -1;

  // Copy strings to kernel space from user virtual addresses.
  if(copyinstr_user((uint)path_addr, path_buf, sizeof(path_buf)) < 0)
    return -1;
  if(copyinstr_user((uint)fstype_addr, fstype_buf, sizeof(fstype_buf)) < 0)
    return -1;
  data_u = 0;
  data_buf = 0;
  if(datalen > 0){
    if(datalen > MOUNT_DATA_MAX)
      return -1;
    if(argint(3, &data_addr) < 0)
      return -1;
    data_u = (uint)data_addr;
  }

  has_dev_override = MNT_HASDEV(flags);
  dev_override = -1;
  if(has_dev_override)
    dev_override = MNT_GETDEV(flags);
  mount_flags = flags & ~MNT_DEVMASK;
  if(has_dev_override && (dev_override < 0 || dev_override >= NDEV))
    return -1;

  if(mount_flags & MNT_REMOUNT)
    return vfs_remount(path_buf, mount_flags & ~MNT_REMOUNT);

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
#if CONFIG_LEGACY_XV6FS
    vfs_xv6fs_init(fs);
#else
    kfree((void*)fs);
    return -1;
#endif
  } else if(memcmp(fstype_buf, "ext2", 5) == 0 ||
             memcmp(fstype_buf, "ext2fs", 7) == 0) {
    vfs_ext2_init(fs);
  } else if(memcmp(fstype_buf, "msdosfs", 8) == 0 ||
            memcmp(fstype_buf, "fat", 4) == 0) {
    vfs_msdosfs_init(fs);
  } else if(memcmp(fstype_buf, "exfat", 6) == 0) {
    vfs_exfat_init(fs);
  } else if(memcmp(fstype_buf, "btrfs", 6) == 0) {
    vfs_btrfs_init(fs);
  } else if(memcmp(fstype_buf, "ufs2", 5) == 0 ||
            memcmp(fstype_buf, "ffs", 4) == 0) {
    vfs_ufs2_init(fs);
  } else if(memcmp(fstype_buf, "isofs", 6) == 0 ||
            memcmp(fstype_buf, "iso9660", 8) == 0 ||
            memcmp(fstype_buf, "cd9660", 7) == 0) {
    vfs_isofs_init(fs);
  } else if(memcmp(fstype_buf, "tmpfs", 6) == 0) {
    vfs_tmpfs_init(fs);
  } else if(memcmp(fstype_buf, "nfs", 4) == 0) {
    vfs_nfs_init(fs);
  } else {
    kfree((void*)fs);
    return -1;
  }

  dev = 255;
  if(memcmp(fstype_buf, "procfs", 7) == 0)
    dev = PROCFSDEV;
  else if(memcmp(fstype_buf, "ext2", 5) == 0 ||
           memcmp(fstype_buf, "ext2fs", 7) == 0)
    dev = has_dev_override ? dev_override : EXT2DEV;
#if CONFIG_LEGACY_XV6FS
  else if(memcmp(fstype_buf, "xv6fs", 6) == 0)
    dev = has_dev_override ? dev_override : ROOTDEV;
#endif
  else if(memcmp(fstype_buf, "msdosfs", 8) == 0 ||
          memcmp(fstype_buf, "fat", 4) == 0)
    dev = has_dev_override ? dev_override : DISK_DEV(3);
  else if(memcmp(fstype_buf, "btrfs", 6) == 0)
    dev = has_dev_override ? dev_override : DISK_DEV(3);
  else if(memcmp(fstype_buf, "ufs2", 5) == 0 ||
          memcmp(fstype_buf, "ffs", 4) == 0)
    dev = has_dev_override ? dev_override : DISK_DEV(3);
  else if(memcmp(fstype_buf, "isofs", 6) == 0 ||
          memcmp(fstype_buf, "iso9660", 8) == 0 ||
          memcmp(fstype_buf, "cd9660", 7) == 0)
    dev = has_dev_override ? dev_override : DISK_DEV(3);
  else if(memcmp(fstype_buf, "tmpfs", 6) == 0){
    if(has_dev_override){
      if(dev_override < TMPFSDEV_BASE ||
         dev_override >= TMPFSDEV_BASE + TMPFSDEV_MAX){
        kfree((void*)fs);
        return -1;
      }
      if(vfs_dev_is_mounted(dev_override)){
        kfree((void*)fs);
        return -1;
      }
      dev = dev_override;
    } else {
      dev = tmpfs_alloc_dev();
    }
  } else if(memcmp(fstype_buf, "nfs", 4) == 0){
    if(has_dev_override){
      if(dev_override < NFSDEV_BASE ||
         dev_override >= NFSDEV_BASE + NFSDEV_MAX){
        kfree((void*)fs);
        return -1;
      }
      if(vfs_dev_is_mounted(dev_override)){
        kfree((void*)fs);
        return -1;
      }
      dev = dev_override;
    } else {
      dev = nfs_alloc_dev();
    }
  }

  if(dev < 0){
    kfree((void*)fs);
    return -1;
  }

  // Stage mount data on heap (one page max) to avoid large stack buffers.
  if(datalen > 0){
    p = myproc();
    pgdir = proc_pgdir(p);
    if(p == 0 || pgdir == 0){
      kfree((void*)fs);
      return -1;
    }
    data_buf = (char*)kmalloc((uint)(datalen + 1));
    if(data_buf == 0){
      kfree((void*)fs);
      return -1;
    }
    if(copyin(pgdir, data_buf, data_u, (uint)datalen) < 0){
      kmalloc_free(data_buf);
      kfree((void*)fs);
      return -1;
    }
    data_buf[datalen] = 0;
  }

  if(vfs_register_mount(fs, dev, mount_flags, path_buf,
                        data_buf ? data_buf : 0, datalen) < 0){
    MOUNTDBG("sys_mount: failed path=%s type=%s dev=%d\n", path_buf, fstype_buf, dev);
    if(data_buf)
      kmalloc_free(data_buf);
    kfree((void*)fs);
    return -1;
  }

  if(data_buf)
    kmalloc_free(data_buf);

  MOUNTDBG("sys_mount: ok path=%s type=%s dev=%d\n", path_buf, fstype_buf, dev);
  return 0;
}

static int
tmpfs_alloc_dev(void)
{
  int dev;

  for(dev = TMPFSDEV_BASE; dev < TMPFSDEV_BASE + TMPFSDEV_MAX; dev++){
    if(!vfs_dev_is_mounted(dev))
      return dev;
  }
  return -1;
}

static int
nfs_alloc_dev(void)
{
  int dev;

  for(dev = NFSDEV_BASE; dev < NFSDEV_BASE + NFSDEV_MAX; dev++){
    if(vfs_dev_is_mounted(dev) == 0)
      return dev;
  }

  return -1;
}

int
sys_umount(void)
{
  int path_addr;
  char path_buf[256];

  if(argint(0, &path_addr) < 0)
    return -1;

  if(copyinstr_user((uint)path_addr, path_buf, sizeof(path_buf)) < 0)
    return -1;

  return vfs_unmount(path_buf);
}

int
sys_exec(void)
{
  int path_addr;
  char path[256];
  char *argv[EXEC_ARGC_MAX];
  // argv_bufs[EXEC_ARGC_MAX][256] would be 32 KB on the kernel stack — instant
  // stack overflow on a 2-page (8 KB) kernel stack.  Instead, allocate one
  // kalloc page (4 KB) and pack all strings into it consecutively.
  // exec() already enforces EXEC_ARG_BYTES_MAX <= 4096, so one page is always
  // sufficient to hold copies of every argument string.
  char *argbuf;
  uint argoff;
  int i;
  int j;
  int bad;
  int rc;
  uint uargv, uarg;

  if(argint(0, &path_addr) < 0 || argint(1, (int*)&uargv) < 0)
    return -1;
  if(copyinstr_user((uint)path_addr, path, sizeof(path)) < 0)
    return -1;

  argbuf = (char*)kalloc();
  if(argbuf == 0)
    return -1;

  memset(argv, 0, sizeof(argv));
  argoff = 0;
  for(i=0;; i++){
    if(i >= NELEM(argv)){
      kfree(argbuf);
      return -1;
    }
    if(fetchint(uargv+4*i, (int*)&uarg) < 0){
      kfree(argbuf);
      return -1;
    }
    if(uarg == 0){
      argv[i] = 0;
      break;
    }
    // Guard against writing past the end of argbuf.
    if(argoff >= PGSIZE){
      kfree(argbuf);
      return -1;
    }
    if(fetchstr_copyin(uarg, argbuf + argoff, PGSIZE - argoff) < 0){
      kfree(argbuf);
      return -1;
    }
    argv[i] = argbuf + argoff;
    argoff += strlen(argbuf + argoff) + 1;
  }

  // Intermittent init/runlevel corruption triage:
  // if dash script argv contains non-printable bytes, dump raw bytes.
  if(argv[0] && strcmp(path, "/bin/dash") == 0 && argv[1]){
    bad = 0;
    for(j = 0; argv[1][j] && j < 64; j++){
      char c = argv[1][j];
      if(c < 32 || c > 126){
        bad = 1;
        break;
      }
    }
    if(bad){
      cprintf("sys_exec: dash argv1 suspect ptr=%p path=%s\n", argv[1], path);
      cprintf("sys_exec: argv1 bytes:");
      for(j = 0; j < 32 && argv[1][j]; j++)
        cprintf(" %02x", (uchar)argv[1][j]);
      cprintf("\n");
    }
  }

  rc = exec(path, argv);
  kfree(argbuf);
  return rc;
}

int
sys_pipe(void)
{
  int fdaddr;
  int kfds[2];
  struct file *rf, *wf;
  int fd0, fd1;
  struct proc *p;

  if(argint(0, &fdaddr) < 0)
    return -1;
  p = myproc();
  if(p == 0 || proc_pgdir(p) == 0)
    return -1;
  if(pipealloc(&rf, &wf) < 0)
    return -1;
  fd0 = -1;
  if((fd0 = fdalloc(rf)) < 0 || (fd1 = fdalloc(wf)) < 0){
    if(fd0 >= 0)
      fd_clear(fd0);
    fileclose(rf);
    fileclose(wf);
    return -1;
  }
  kfds[0] = fd0;
  kfds[1] = fd1;
  if(copyout(proc_pgdir(p), (uint)fdaddr, kfds, sizeof(kfds)) < 0){
    fd_clear(fd0);
    fd_clear(fd1);
    fileclose(rf);
    fileclose(wf);
    return -1;
  }
  return 0;
}

int
sys_poll(void)
{
  int nfds;
  int timeout_ms;
  int ufds_addr;
  uint ufds_u;
  struct kpollfd *kfds;
  struct proc *curproc;
  pde_t *pgdir;
  int timeout_ticks;
  uint start;
  uint now;
  int ready;

  if(argint(1, &nfds) < 0 || argint(2, &timeout_ms) < 0)
    return -1;
  if(nfds < 0)
    return -1;
  curproc = myproc();
  pgdir = proc_pgdir(curproc);
  if(curproc == 0 || pgdir == 0 || nfds > proc_fd_limit(curproc))
    return -1;

  if(nfds == 0)
    ufds_u = 0;
  else {
    if(argint(0, &ufds_addr) < 0)
      return -1;
    ufds_u = (uint)ufds_addr;
  }

  if(nfds == 0){
    kfds = 0;
  } else {
    kfds = (struct kpollfd*)kmalloc(nfds * sizeof(*kfds));
    if(kfds == 0)
      return -1;
    if(copyin(pgdir, kfds, ufds_u, nfds * sizeof(*kfds)) < 0){
      kmalloc_free(kfds);
      return -1;
    }
  }

  if(timeout_ms < 0)
    timeout_ticks = -1;
  else
    timeout_ticks = timeout_ms_to_ticks(timeout_ms);

  acquire(&tickslock);
  start = ticks;
  release(&tickslock);

  for(;;){
    ready = poll_scan(kfds, nfds);
    if(ready > 0){
      if(nfds > 0 && copyout(pgdir, ufds_u, kfds,
                             nfds * sizeof(*kfds)) < 0){
        if(kfds)
          kmalloc_free(kfds);
        return -1;
      }
      if(kfds)
        kmalloc_free(kfds);
      return ready;
    }
    if(timeout_ms == 0){
      if(nfds > 0 && copyout(pgdir, ufds_u, kfds,
                             nfds * sizeof(*kfds)) < 0){
        if(kfds)
          kmalloc_free(kfds);
        return -1;
      }
      if(kfds)
        kmalloc_free(kfds);
      return 0;
    }
    if(myproc()->killed){
      if(kfds)
        kmalloc_free(kfds);
      return -1;
    }

    if(timeout_ticks >= 0){
      acquire(&tickslock);
      now = ticks;
      if(now - start >= (uint)timeout_ticks){
        release(&tickslock);
        if(nfds > 0 && copyout(pgdir, ufds_u, kfds,
                               nfds * sizeof(*kfds)) < 0){
          if(kfds)
            kmalloc_free(kfds);
          return -1;
        }
        if(kfds)
          kmalloc_free(kfds);
        return 0;
      }
      sleep(&ticks, &tickslock);
      release(&tickslock);
    } else {
      acquire(&tickslock);
      sleep(&ticks, &tickslock);
      release(&tickslock);
    }
  }
}

int
sys_select(void)
{
  int nfds;
  int words;
  int bytes;
  int read_addr;
  int write_addr;
  int except_addr;
  int timeout_addr;
  int *buf;
  int *in_read;
  int *in_write;
  int *in_except;
  int *out_read;
  int *out_write;
  int *out_except;
  int timeout_ticks;
  int timeout_ms;
  uint start;
  uint now;
  int ready;
  struct ktimeval tv;
  struct proc *curproc;
  pde_t *pgdir;

  if(argint(0, &nfds) < 0 || argint(1, &read_addr) < 0 || argint(2, &write_addr) < 0 ||
     argint(3, &except_addr) < 0 || argint(4, &timeout_addr) < 0)
    return -1;

  curproc = myproc();
    pgdir = proc_pgdir(curproc);
  if(nfds < 0 || curproc == 0 ||
      pgdir == 0 ||
     nfds > proc_fd_limit(curproc) ||
     nfds > SELECT_FD_SETSIZE)
    return -1;

  words = (nfds + SELECT_BITS_PER_WORD - 1) / SELECT_BITS_PER_WORD;
  bytes = words * sizeof(int);

  // Pack six fd-set bitmaps into one kmalloc buffer.
  if(words == 0)
    buf = 0;
  else {
    buf = (int*)kmalloc((uint)(6 * bytes));
    if(buf == 0)
      return -1;
  }

  in_read = buf;
  in_write = buf ? (buf + words) : 0;
  in_except = buf ? (buf + 2 * words) : 0;
  out_read = buf ? (buf + 3 * words) : 0;
  out_write = buf ? (buf + 4 * words) : 0;
  out_except = buf ? (buf + 5 * words) : 0;

  if(words > 0){
    memset(in_read, 0, bytes);
    memset(in_write, 0, bytes);
    memset(in_except, 0, bytes);
  }

  if(read_addr){
    uint read_u = (uint)read_addr;
    if(read_u >= curproc->sz || (uint)bytes > curproc->sz - read_u){
      if(buf)
        kmalloc_free(buf);
      return -1;
    }
    if(bytes > 0 && copyin(pgdir, in_read, (uint)read_addr, bytes) < 0){
      if(buf)
        kmalloc_free(buf);
      return -1;
    }
  }
  if(write_addr){
    uint write_u = (uint)write_addr;
    if(write_u >= curproc->sz || (uint)bytes > curproc->sz - write_u){
      if(buf)
        kmalloc_free(buf);
      return -1;
    }
    if(bytes > 0 && copyin(pgdir, in_write, (uint)write_addr, bytes) < 0){
      if(buf)
        kmalloc_free(buf);
      return -1;
    }
  }
  if(except_addr){
    uint except_u = (uint)except_addr;
    if(except_u >= curproc->sz || (uint)bytes > curproc->sz - except_u){
      if(buf)
        kmalloc_free(buf);
      return -1;
    }
    if(bytes > 0 && copyin(pgdir, in_except, (uint)except_addr, bytes) < 0){
      if(buf)
        kmalloc_free(buf);
      return -1;
    }
  }

  timeout_ticks = -1;
  if(timeout_addr){
    uint timeout_u = (uint)timeout_addr;
    if(timeout_u >= curproc->sz || sizeof(tv) > curproc->sz - timeout_u){
      if(buf)
        kmalloc_free(buf);
      return -1;
    }
    if(copyin(pgdir, &tv, (uint)timeout_addr, sizeof(tv)) < 0){
      if(buf)
        kmalloc_free(buf);
      return -1;
    }
    if(tv.tv_sec < 0 || tv.tv_usec < 0 || tv.tv_usec >= 1000000){
      if(buf)
        kmalloc_free(buf);
      return -1;
    }
    timeout_ms = tv.tv_sec * 1000 + (tv.tv_usec + 999) / 1000;
    timeout_ticks = timeout_ms_to_ticks(timeout_ms);
  }

  acquire(&tickslock);
  start = ticks;
  release(&tickslock);

  for(;;){
    ready = select_scan(nfds,
                        read_addr ? in_read : 0,
                        write_addr ? in_write : 0,
                        except_addr ? in_except : 0,
                        out_read,
                        out_write,
                        out_except,
                        words);
    if(ready > 0){
      if(read_addr && bytes > 0 && copyout(pgdir, (uint)read_addr, out_read, bytes) < 0){
        if(buf)
          kmalloc_free(buf);
        return -1;
      }
      if(write_addr && bytes > 0 && copyout(pgdir, (uint)write_addr, out_write, bytes) < 0){
        if(buf)
          kmalloc_free(buf);
        return -1;
      }
      if(except_addr && bytes > 0 && copyout(pgdir, (uint)except_addr, out_except, bytes) < 0){
        if(buf)
          kmalloc_free(buf);
        return -1;
      }
      if(buf)
        kmalloc_free(buf);
      return ready;
    }

    if(timeout_addr && timeout_ticks == 0){
      if(bytes > 0)
        memset(out_read, 0, bytes);
      if(read_addr && bytes > 0 && copyout(pgdir, (uint)read_addr, out_read, bytes) < 0){
        if(buf)
          kmalloc_free(buf);
        return -1;
      }
      if(bytes > 0)
        memset(out_write, 0, bytes);
      if(write_addr && bytes > 0 && copyout(pgdir, (uint)write_addr, out_write, bytes) < 0){
        if(buf)
          kmalloc_free(buf);
        return -1;
      }
      if(bytes > 0)
        memset(out_except, 0, bytes);
      if(except_addr && bytes > 0 && copyout(pgdir, (uint)except_addr, out_except, bytes) < 0){
        if(buf)
          kmalloc_free(buf);
        return -1;
      }
      if(buf)
        kmalloc_free(buf);
      return 0;
    }

    if(myproc()->killed){
      if(buf)
        kmalloc_free(buf);
      return -1;
    }

    if(timeout_ticks >= 0){
      acquire(&tickslock);
      now = ticks;
      if(now - start >= (uint)timeout_ticks){
        release(&tickslock);
        if(bytes > 0)
          memset(out_read, 0, bytes);
        if(read_addr && bytes > 0 && copyout(pgdir, (uint)read_addr, out_read, bytes) < 0){
          if(buf)
            kmalloc_free(buf);
          return -1;
        }
        if(bytes > 0)
          memset(out_write, 0, bytes);
        if(write_addr && bytes > 0 && copyout(pgdir, (uint)write_addr, out_write, bytes) < 0){
          if(buf)
            kmalloc_free(buf);
          return -1;
        }
        if(bytes > 0)
          memset(out_except, 0, bytes);
        if(except_addr && bytes > 0 && copyout(pgdir, (uint)except_addr, out_except, bytes) < 0){
          if(buf)
            kmalloc_free(buf);
          return -1;
        }
        if(buf)
          kmalloc_free(buf);
        return 0;
      }
      sleep(&ticks, &tickslock);
      release(&tickslock);
    } else {
      acquire(&tickslock);
      sleep(&ticks, &tickslock);
      release(&tickslock);
    }
  }
}

// lseek - reposition read/write file offset
// On i386 the syscall ABI pushes arguments as 32-bit words.  Since off_t is
// int64_t (8 bytes), the compiler pushes it as two consecutive 32-bit words:
//   arg0: fd          (esp+4)
//   arg1: offset_lo   (esp+8)   — low  32 bits of the 64-bit offset
//   arg2: offset_hi   (esp+12)  — high 32 bits of the 64-bit offset
//   arg3: whence      (esp+16)
// Returns off_t via edx:eax (i386 int64_t return convention):
//   eax = low 32 bits, edx = high 32 bits (written into tf->edx before return).
int
sys_lseek(void)
{
  struct file *f;
  int offset_lo, offset_hi;
  int whence;
  int64_t offset;
  int64_t newoff;

  if(argfd(0, 0, &f) < 0 || argint(1, &offset_lo) < 0 ||
     argint(2, &offset_hi) < 0 || argint(3, &whence) < 0)
    return -1;

  offset = ((int64_t)(uint)offset_hi << 32) | (uint)offset_lo;

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
    newoff = (int64_t)f->off + offset;
    break;
  case 2: // SEEK_END
    newoff = (int64_t)f->ip->size + offset;
    break;
  default:
    iunlock(f->ip);
    return -1;
  }

  if(newoff < 0){
    iunlock(f->ip);
    return -1;
  }

  f->off = newoff;
  iunlock(f->ip);
  // Return 64-bit offset via edx:eax (i386 int64_t return convention).
  myproc()->tf->edx = (uint)((uint64_t)newoff >> 32);
  return (int)(uint)(newoff & 0xffffffff);
}

int
sys_truncate(void)
{
  int path_addr;
  char path[256];
  int len_lo, len_hi;
  int64_t len;
  struct inode *ip;
  const struct vnode_ops *ops;

  if(argint(0, &path_addr) < 0 || argint(1, &len_lo) < 0 || argint(2, &len_hi) < 0)
    return -1;
  if(copyinstr_user((uint)path_addr, path, sizeof(path)) < 0)
    return -1;

  len = ((int64_t)(uint)len_hi << 32) | (uint)len_lo;
  if(len < 0)
    return -1;

  begin_op();
  ip = vfs_resolve(path);
  if(ip == 0){
    end_op();
    return -1;
  }

  ilock(ip);
  if(iaccess(ip, IACC_WRITE) < 0){
    iunlockput(ip);
    end_op();
    return -1;
  }

  if((uint64_t)len == ip->size){
    iunlockput(ip);
    end_op();
    return 0;
  }

  if(len != 0){
    iunlockput(ip);
    end_op();
    return -1;
  }

  ops = vfs_dev_vops(ip->dev);
  if(ops == 0 || ops->truncate == 0 || ops->truncate(ip) < 0){
    iunlockput(ip);
    end_op();
    return -1;
  }

  iunlockput(ip);
  end_op();
  return 0;
}

int
sys_ftruncate(void)
{
  int len_lo, len_hi;
  int64_t len;
  struct file *f;
  const struct vnode_ops *ops;

  if(argfd(0, 0, &f) < 0 || argint(1, &len_lo) < 0 || argint(2, &len_hi) < 0)
    return -1;

  len = ((int64_t)(uint)len_hi << 32) | (uint)len_lo;
  if(len < 0)
    return -1;

  if(f->type != FD_INODE || f->ip == 0)
    return -1;

  begin_op();
  ilock(f->ip);
  if(iaccess(f->ip, IACC_WRITE) < 0){
    iunlock(f->ip);
    end_op();
    return -1;
  }

  if((uint64_t)len == f->ip->size){
    iunlock(f->ip);
    end_op();
    return 0;
  }

  if(len != 0){
    iunlock(f->ip);
    end_op();
    return -1;
  }

  ops = vfs_dev_vops(f->ip->dev);
  if(ops == 0 || ops->truncate == 0 || ops->truncate(f->ip) < 0){
    iunlock(f->ip);
    end_op();
    return -1;
  }

  if(f->off > f->ip->size)
    f->off = f->ip->size;

  iunlock(f->ip);
  end_op();
  return 0;
}

// _llseek / lseek64 — 64-bit seek with Linux-compatible 5-arg ABI:
//   arg0: fd
//   arg1: offset_high  (high 32 bits of 64-bit offset)
//   arg2: offset_low   (low  32 bits of 64-bit offset)
//   arg3: result       (userspace loff_t* to receive the new position)
//   arg4: whence
// Returns 0 on success (result written), -1 on failure.
int
sys_lseek64(void)
{
  struct file *f;
  int offset_hi, offset_lo;
  int whence;
  int result_addr;
  int64_t offset, newoff;

  if(argfd(0, 0, &f) < 0 || argint(1, &offset_hi) < 0 ||
     argint(2, &offset_lo) < 0 || argint(3, &result_addr) < 0 ||
     argint(4, &whence) < 0)
    return -1;

  offset = ((int64_t)(uint)offset_hi << 32) | (uint)offset_lo;

  if(f->type == FD_PIPE || f->type == FD_SOCKET)
    return -1;
  if(f->type != FD_INODE)
    return -1;

  ilock(f->ip);

  switch(whence){
  case 0: newoff = offset;                         break; // SEEK_SET
  case 1: newoff = (int64_t)f->off + offset;       break; // SEEK_CUR
  case 2: newoff = (int64_t)f->ip->size + offset;  break; // SEEK_END
  default:
    iunlock(f->ip);
    return -1;
  }

  if(newoff < 0){
    iunlock(f->ip);
    return -1;
  }

  f->off = newoff;
  iunlock(f->ip);

  if(result_addr != 0){
    if(copyout(proc_pgdir(myproc()), (uint)result_addr,
               (char*)&newoff, sizeof(newoff)) < 0)
      return -1;
  }
  return 0;
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
  if(newfd < 0 || newfd >= proc_fd_limit(curproc))
    return -1;

  // If oldfd == newfd, just return newfd (no-op per POSIX)
  if(oldfd == newfd)
    return newfd;

  if(fd_ensure_slot(curproc, newfd) < 0)
    return -1;

  // If newfd is already open, close it first
  if(curproc->fdtable->entries[newfd] != 0){
    struct file *oldf;
    oldf = curproc->fdtable->entries[newfd];
    fd_clear(newfd);
    fileclose(oldf);
  }

  // Duplicate the file reference; new descriptor does not inherit FD_CLOEXEC
  curproc->fdtable->entries[newfd] = f;
  curproc->fdtable->fdflags[newfd] = 0;
  filedup(f);

  return newfd;
}

// fcntl - file control
// Implements F_DUPFD, F_GETFD, F_SETFD, F_GETFL, F_SETFL, F_DUPFD_CLOEXEC
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
  case 0: // F_DUPFD - duplicate fd to lowest available >= arg; new fd has no FD_CLOEXEC
    if(argint(2, &arg) < 0)
      return -1;
    if(arg < 0 || arg >= proc_fd_limit(curproc))
      return -1;
    for(int i = arg; i < proc_fd_limit(curproc); i++){
      if(fd_ensure_slot(curproc, i) < 0)
        return -1;
      if(curproc->fdtable->entries[i] == 0){
        curproc->fdtable->entries[i] = f;
        curproc->fdtable->fdflags[i] = 0;
        filedup(f);
        return i;
      }
    }
    return -1;

  case 1: // F_GETFD - get file descriptor flags (FD_CLOEXEC etc.)
    return (int)(curproc->fdtable->fdflags[fd] & FD_CLOEXEC);

  case 2: // F_SETFD - set file descriptor flags
    if(argint(2, &arg) < 0)
      return -1;
    curproc->fdtable->fdflags[fd] = (uint8_t)(arg & FD_CLOEXEC);
    return 0;

  case 3: // F_GETFL - get file status flags
    flags = 0;
    if(f->readable && f->writable)
      flags = O_RDWR;
    else if(f->writable)
      flags = O_WRONLY;
    else
      flags = O_RDONLY;

    if(f->type == FD_INODE && f->ip && f->ip->type == T_DEV &&
       f->ip->major == AUDIODEV && f->ip->minor != 0){
      if(audio_get_nonblock(f) > 0)
        flags |= O_NONBLOCK;
    }
    if(f->type == FD_INODE && f->ip && f->ip->type == T_DEV &&
       f->ip->major == TUNTAPDEV){
      if(tuntap_get_nonblock(f) > 0)
        flags |= O_NONBLOCK;
    }
    return flags;

  case 4: // F_SETFL - set file status flags (audio O_NONBLOCK tracked)
    if(argint(2, &arg) < 0)
      return -1;
    if(f->type == FD_INODE && f->ip && f->ip->type == T_DEV &&
       f->ip->major == AUDIODEV && f->ip->minor != 0){
      if(audio_set_nonblock(f, (arg & O_NONBLOCK) != 0) < 0)
        return -1;
    }
    if(f->type == FD_INODE && f->ip && f->ip->type == T_DEV &&
       f->ip->major == TUNTAPDEV){
      if(tuntap_set_nonblock(f, (arg & O_NONBLOCK) != 0) < 0)
        return -1;
    }
    return 0;

  case 1030: // F_DUPFD_CLOEXEC - duplicate with FD_CLOEXEC set on new descriptor
    if(argint(2, &arg) < 0)
      return -1;
    if(arg < 0 || arg >= proc_fd_limit(curproc))
      return -1;
    for(int i = arg; i < proc_fd_limit(curproc); i++){
      if(fd_ensure_slot(curproc, i) < 0)
        return -1;
      if(curproc->fdtable->entries[i] == 0){
        curproc->fdtable->entries[i] = f;
        curproc->fdtable->fdflags[i] = FD_CLOEXEC;
        filedup(f);
        return i;
      }
    }
    return -1;

  default:
    return -1;
  }
}

// sys_symlink - create a symbolic link
// symlink(target, linkpath)
int
sys_symlink(void)
{
  int target_addr, linkpath_addr;
  char target[256], linkpath[256];
  const struct vnode_ops *ops;
  struct inode *dp;
  char name[DIRSIZ];

  if(argint(0, &target_addr) < 0 || argint(1, &linkpath_addr) < 0)
    return -1;
  if(copyinstr_user((uint)target_addr, target, sizeof(target)) < 0 ||
     copyinstr_user((uint)linkpath_addr, linkpath, sizeof(linkpath)) < 0)
    return -1;

  begin_op();
  dp = vfs_resolve_parent(linkpath, name);
  if(dp == 0){
    end_op();
    return -1;
  }

  ilock(dp);
  ops = vfs_dev_vops(dp->dev);
  if(ops == 0 || ops->symlink == 0){
    iunlockput(dp);
    end_op();
    return -1;
  }

  if(ops->symlink(dp, name, target) < 0){
    iunlockput(dp);
    end_op();
    return -1;
  }

  iunlockput(dp);
  end_op();
  return 0;
}

// sys_readlink - read the target of a symbolic link
// readlink(path, buf, bufsiz)
int
sys_readlink(void)
{
  int path_addr;
  char path[256];
  int bufaddr;
  char *kbuf;
  int bufsiz;
  const struct vnode_ops *ops;
  struct inode *ip;
  int n;
  struct proc *p;
  pde_t *pgdir;

  if(argint(0, &path_addr) < 0 || argint(2, &bufsiz) < 0)
    return -1;
  if(copyinstr_user((uint)path_addr, path, sizeof(path)) < 0)
    return -1;
  if(bufsiz < 0)
    return -1;
  if(argint(1, &bufaddr) < 0)
    return -1;

  p = myproc();
  pgdir = proc_pgdir(p);
  if(p == 0 || pgdir == 0)
    return -1;

  if(bufsiz == 0)
    return 0;

  kbuf = (char*)kmalloc(bufsiz);
  if(kbuf == 0)
    return -1;

  begin_op();
  // vfs_namei does NOT follow symlinks — we need the link inode itself.
  ip = vfs_namei(path);
  if(ip == 0){
    end_op();
    kmalloc_free(kbuf);
    return -1;
  }

  ilock(ip);

  // Verify it's a symlink
  if(ip->type != T_SYMLINK){
    iunlockput(ip);
    end_op();
    kmalloc_free(kbuf);
    return -1;
  }

  ops = vfs_dev_vops(ip->dev);
  if(ops == 0 || ops->readlink == 0){
    iunlockput(ip);
    end_op();
    kmalloc_free(kbuf);
    return -1;
  }

  n = ops->readlink(ip, kbuf, bufsiz);
  iunlockput(ip);
  end_op();

  if(n > 0 && copyout(pgdir, (uint)bufaddr, kbuf, n) < 0){
    kmalloc_free(kbuf);
    return -1;
  }

  kmalloc_free(kbuf);
  return n;
}

// sys_lstat - stat without following symlinks
// lstat(path, statbuf)
int
sys_lstat(void)
{
  int path_addr;
  char path[256];
  const struct vnode_ops *ops;
  int rc;
  struct stat st;
  int staddr;
  struct proc *p;
  pde_t *pgdir;
  struct inode *ip;

  if(argint(0, &path_addr) < 0 || argint(1, &staddr) < 0)
    return -1;
  if(copyinstr_user((uint)path_addr, path, sizeof(path)) < 0)
    return -1;

  begin_op();
  // lstat must NOT follow symlinks — use the no-follow vfs_namei.
  if((ip = vfs_namei(path)) == 0){
    end_op();
    return -1;
  }
  ilock(ip);
  ops = vfs_dev_vops(ip->dev);
  if(ops && ops->stat){
    rc = ops->stat(ip, &st);
  } else {
    rc = 0;
    stati(ip, &st);
  }
  iunlockput(ip);
  end_op();
  if(rc < 0)
    return rc;

  p = myproc();
  pgdir = proc_pgdir(p);
  if(p == 0 || pgdir == 0)
    return -1;
  if(copyout(pgdir, (uint)staddr, &st, sizeof(st)) < 0)
    return -1;
  return 0;
}

int
sys_loopsetup(void)
{
  int loopnum;
  int path_addr;
  char path[256];
  int offset;
  int nblocks;
  struct inode *ip;

  if(argint(0, &loopnum) < 0)
    return -1;
  if(argint(1, &path_addr) < 0)
    return -1;
  if(copyinstr_user((uint)path_addr, path, sizeof(path)) < 0)
    return -1;
  if(argint(2, &offset) < 0)
    return -1;
  if(argint(3, &nblocks) < 0)
    return -1;

  // Open the backing file
  begin_op();
  if((ip = vfs_namei(path)) == 0){
    end_op();
    return -1;
  }
  end_op();

  // Setup the loop device
  if(loop_setup(loopnum, ip, offset, nblocks) < 0){
    iput(ip);
    return -1;
  }

  // loop_setup takes a reference, we can release ours
  iput(ip);
  return 0;
}

int
sys_loopteardown(void)
{
  int loopnum;

  if(argint(0, &loopnum) < 0)
    return -1;

  return loop_teardown(loopnum);
}

int
sys_loopstatus(void)
{
  int loopnum;
  int backing_inum_addr;
  int offset_addr;
  int nblocks_addr;
  int flags_addr;
  uint backing_inum;
  uint offset;
  uint nblocks;
  uint flags;
  struct proc *p;
  pde_t *pgdir;

  if(argint(0, &loopnum) < 0)
    return -1;
  if(argint(1, &backing_inum_addr) < 0)
    return -1;
  if(argint(2, &offset_addr) < 0)
    return -1;
  if(argint(3, &nblocks_addr) < 0)
    return -1;
  if(argint(4, &flags_addr) < 0)
    return -1;

  p = myproc();
  pgdir = proc_pgdir(p);
  if(p == 0 || pgdir == 0)
    return -1;

  if(loop_status(loopnum, &backing_inum, &offset, &nblocks, &flags) < 0)
    return -1;
  if(copyout(pgdir, (uint)backing_inum_addr, &backing_inum, sizeof(backing_inum)) < 0)
    return -1;
  if(copyout(pgdir, (uint)offset_addr, &offset, sizeof(offset)) < 0)
    return -1;
  if(copyout(pgdir, (uint)nblocks_addr, &nblocks, sizeof(nblocks)) < 0)
    return -1;
  if(copyout(pgdir, (uint)flags_addr, &flags, sizeof(flags)) < 0)
    return -1;
  return 0;
}
