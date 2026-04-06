#include "types.h"
#include "defs.h"
#include "param.h"
#include "memlayout.h"
#include "proc.h"
#include "spinlock.h"
#include "sleeplock.h"
#include "vfs.h"
#include "fs.h"
#include "file.h"
#include "stat.h"

#define TMPFS_PAGE_SIZE 4096
#define TMPFS_NAME_MAX DIRSIZ
#define TMPFS_DHASH_SIZE 32

static char tmpfs_zero_page[TMPFS_PAGE_SIZE];

struct tmpfs_dirent {
  char name[TMPFS_NAME_MAX + 1];
  struct tmpfs_node *node;
  struct tmpfs_dirent *next;
  struct tmpfs_dirent *hash_next;
};

struct tmpfs_node {
  uint inum;
  short type;
  short nlink;
  short uid;
  short gid;
  short major;
  short minor;
  int mode;
  uint size;
  struct tmpfs_node *parent;
  struct tmpfs_dirent *children;
  struct tmpfs_dirent *child_hash[TMPFS_DHASH_SIZE];
  struct tmpfs_page *pages;
  char *symlink;
  uint symlink_len;
};

struct tmpfs_page {
  uint index;
  char *data;
  struct tmpfs_page *next;
};

struct tmpfs_mount_data {
  struct spinlock lock;
  uint max_bytes;
  uint used_bytes;
  uint next_inum;
  int dev;
  struct tmpfs_node *root;
};

static struct tmpfs_mount_data* tmpfs_data_for_dev(uint dev);
static struct tmpfs_node* tmpfs_inode_node(struct inode *ip);
static struct tmpfs_node* tmpfs_alloc_node(struct tmpfs_mount_data *md, short type,
                                           struct tmpfs_node *parent);
static void tmpfs_free_node(struct tmpfs_mount_data *md, struct tmpfs_node *node);
static struct tmpfs_dirent* tmpfs_dirent_lookup(struct tmpfs_node *dir, char *name);
static int tmpfs_dirent_add(struct tmpfs_node *dir, char *name, struct tmpfs_node *node);
static int tmpfs_dirent_remove(struct tmpfs_node *dir, char *name, struct tmpfs_node **out);
static int tmpfs_dir_empty(struct tmpfs_node *dir);
static int tmpfs_read_pages(struct tmpfs_node *node, char *dst, uint off, uint n);
static int tmpfs_write_pages(struct tmpfs_mount_data *md, struct tmpfs_node *node,
                             char *src, uint off, uint n);
static int tmpfs_truncate_node(struct tmpfs_mount_data *md, struct tmpfs_node *node, uint size);
static int tmpfs_parse_size(const char *data, uint *out);
static struct tmpfs_page* tmpfs_page_lookup(struct tmpfs_node *node, uint index);
static int tmpfs_page_get(struct tmpfs_node *node, uint index, struct tmpfs_page **out);
static struct tmpfs_node* tmpfs_find_node(struct tmpfs_node *node, uint inum);
static uint tmpfs_name_hash(char *name);

static struct tmpfs_mount_data*
tmpfs_data_for_dev(uint dev)
{
  return (struct tmpfs_mount_data*)vfs_dev_fs_data(dev);
}

int
tmpfs_block_usage(uint dev, uint *total_blocks, uint *free_blocks, uint *block_size)
{
  struct tmpfs_mount_data *md;
  uint total;
  uint used;

  if(total_blocks == 0 || free_blocks == 0 || block_size == 0)
    return -1;

  md = tmpfs_data_for_dev(dev);
  if(md == 0)
    return -1;

  total = md->max_bytes;
  used = md->used_bytes;
  if(used > total)
    used = total;

  *total_blocks = total;
  *free_blocks = total - used;
  *block_size = 1;
  return 0;
}

static struct tmpfs_node*
tmpfs_inode_node(struct inode *ip)
{
  if(ip == 0)
    return 0;
  return (struct tmpfs_node*)(uint)ip->addrs[0];
}

static struct inode*
tmpfs_make_inode(struct tmpfs_mount_data *md, struct tmpfs_node *node)
{
  struct inode *ip;

  if(md == 0 || node == 0)
    return 0;

  ip = iget(md->dev, node->inum);
  if(ip == 0)
    return 0;

  acquiresleep(&ip->lock);
  ip->dev = md->dev;
  ip->inum = node->inum;
  ip->valid = 1;
  ip->type = node->type;
  ip->major = node->major;
  ip->minor = node->minor;
  ip->nlink = node->nlink;
  ip->uid = node->uid;
  ip->gid = node->gid;
  ip->mode = node->mode;
  ip->size = node->size;
  memset(ip->addrs, 0, sizeof(ip->addrs));
  ip->addrs[0] = (uint)node;
  releasesleep(&ip->lock);

  return ip;
}

static struct tmpfs_node*
tmpfs_alloc_node(struct tmpfs_mount_data *md, short type, struct tmpfs_node *parent)
{
  struct tmpfs_node *node;

  node = (struct tmpfs_node*)kalloc();
  if(node == 0)
    return 0;
  memset(node, 0, sizeof(*node));

  node->inum = md->next_inum++;
  node->type = type;
  node->nlink = 1;
  node->uid = 0;
  node->gid = 0;
  node->major = 0;
  node->minor = 0;
  node->mode = (M_IRUSR | M_IWUSR | M_IXUSR |
                M_IRGRP | M_IWGRP | M_IXGRP |
                M_IROTH | M_IWOTH | M_IXOTH);
  node->size = 0;
  node->parent = parent;
  return node;
}

static void
tmpfs_free_node(struct tmpfs_mount_data *md, struct tmpfs_node *node)
{
  (void)md;
  if(md == 0 || node == 0)
    return;

  while(node->pages){
    struct tmpfs_page *page;

    page = node->pages;
    node->pages = page->next;
    if(page->data)
      kfree(page->data);
    kfree((char*)page);
  }

  if(node->symlink)
    kfree(node->symlink);

  kfree((char*)node);
}

static struct tmpfs_dirent*
tmpfs_dirent_lookup(struct tmpfs_node *dir, char *name)
{
  struct tmpfs_dirent *de;
  uint h;

  if(dir == 0 || name == 0)
    return 0;
  h = tmpfs_name_hash(name) & (TMPFS_DHASH_SIZE - 1);
  for(de = dir->child_hash[h]; de; de = de->hash_next){
    if(strncmp(de->name, name, TMPFS_NAME_MAX + 1) == 0)
      return de;
  }
  return 0;
}

static uint
tmpfs_name_hash(char *name)
{
  uint h;
  int i;

  h = 2166136261u;
  for(i = 0; name && name[i]; i++){
    h ^= (uint)(uchar)name[i];
    h *= 16777619u;
  }
  return h;
}

static int
tmpfs_dirent_add(struct tmpfs_node *dir, char *name, struct tmpfs_node *node)
{
  struct tmpfs_dirent *de;
  int nlen;
  uint h;

  if(dir == 0 || name == 0 || node == 0)
    return -1;
  nlen = strlen(name);
  if(nlen <= 0 || nlen > TMPFS_NAME_MAX)
    return -1;
  if(tmpfs_dirent_lookup(dir, name))
    return -1;

  de = (struct tmpfs_dirent*)kalloc();
  if(de == 0)
    return -1;
  memset(de, 0, sizeof(*de));
  safestrcpy(de->name, name, sizeof(de->name));
  de->node = node;
  h = tmpfs_name_hash(name) & (TMPFS_DHASH_SIZE - 1);
  de->hash_next = dir->child_hash[h];
  dir->child_hash[h] = de;
  de->next = dir->children;
  dir->children = de;
  return 0;
}

static int
tmpfs_dirent_remove(struct tmpfs_node *dir, char *name, struct tmpfs_node **out)
{
  struct tmpfs_dirent *prev;
  struct tmpfs_dirent *de;
  struct tmpfs_dirent *hprev;
  struct tmpfs_dirent *hde;
  uint h;

  if(out)
    *out = 0;
  if(dir == 0 || name == 0)
    return -1;

  prev = 0;
  h = tmpfs_name_hash(name) & (TMPFS_DHASH_SIZE - 1);
  hprev = 0;
  for(hde = dir->child_hash[h]; hde; hde = hde->hash_next){
    if(strncmp(hde->name, name, TMPFS_NAME_MAX + 1) == 0)
      break;
    hprev = hde;
  }
  if(hde == 0)
    return -1;

  for(de = dir->children; de; de = de->next){
    if(strncmp(de->name, name, TMPFS_NAME_MAX + 1) == 0){
      if(prev)
        prev->next = de->next;
      else
        dir->children = de->next;
      if(hprev)
        hprev->hash_next = hde->hash_next;
      else
        dir->child_hash[h] = hde->hash_next;
      if(out)
        *out = de->node;
      kfree((char*)de);
      return 0;
    }
    prev = de;
  }
  return -1;
}

static int
tmpfs_dir_empty(struct tmpfs_node *dir)
{
  return dir && dir->children == 0;
}

static int
tmpfs_read_pages(struct tmpfs_node *node, char *dst, uint off, uint n)
{
  uint done;
  int user_dst;
  struct proc *p;

  if(node == 0 || dst == 0)
    return -1;
  if(off >= node->size)
    return 0;
  if(off + n > node->size)
    n = node->size - off;

  user_dst = ((uint)dst < KERNBASE);
  p = user_dst ? myproc() : 0;
  if(user_dst && (p == 0 || p->pgdir == 0))
    return -1;

  done = 0;
  while(done < n){
    uint page_index;
    uint page_off;
    uint chunk;

    page_index = (off + done) / TMPFS_PAGE_SIZE;
    page_off = (off + done) % TMPFS_PAGE_SIZE;
    chunk = TMPFS_PAGE_SIZE - page_off;
    if(chunk > n - done)
      chunk = n - done;
    struct tmpfs_page *page;

    page = tmpfs_page_lookup(node, page_index);
    if(user_dst){
      char *src;
      src = (page == 0 || page->data == 0)
          ? tmpfs_zero_page
          : (page->data + page_off);
      if(copyout(p->pgdir, (uint)(dst + done), src, chunk) < 0)
        return -1;
    } else {
      if(page == 0 || page->data == 0)
        memset(dst + done, 0, chunk);
      else
        memmove(dst + done, page->data + page_off, chunk);
    }
    done += chunk;
  }

  return n;
}

static int
tmpfs_write_pages(struct tmpfs_mount_data *md, struct tmpfs_node *node,
                  char *src, uint off, uint n)
{
  uint new_size;
  uint done;
  int user_src;
  struct proc *p;

  if(md == 0 || node == 0 || src == 0)
    return -1;

  new_size = off + n;
  if(new_size > node->size){
    uint delta;
    delta = new_size - node->size;
    if(md->used_bytes + delta > md->max_bytes)
      return -1;
  }

  user_src = ((uint)src < KERNBASE);
  p = user_src ? myproc() : 0;
  if(user_src && (p == 0 || p->pgdir == 0))
    return -1;

  done = 0;
  while(done < n){
    uint page_index;
    uint page_off;
    uint chunk;
    struct tmpfs_page *page;

    page_index = (off + done) / TMPFS_PAGE_SIZE;
    page_off = (off + done) % TMPFS_PAGE_SIZE;
    chunk = TMPFS_PAGE_SIZE - page_off;
    if(chunk > n - done)
      chunk = n - done;
    if(tmpfs_page_get(node, page_index, &page) < 0)
      return -1;
    if(user_src){
      if(copyin(p->pgdir, page->data + page_off, (uint)(src + done), chunk) < 0)
        return -1;
    } else {
      memmove(page->data + page_off, src + done, chunk);
    }
    done += chunk;
  }

  if(new_size > node->size){
    md->used_bytes += new_size - node->size;
    node->size = new_size;
  }
  return n;
}

static int
tmpfs_truncate_node(struct tmpfs_mount_data *md, struct tmpfs_node *node, uint size)
{
  uint old_size;
  uint need_pages;
  struct tmpfs_page *cur;
  struct tmpfs_page *prev;

  if(md == 0 || node == 0)
    return -1;

  old_size = node->size;
  if(size >= old_size)
    return 0;

  need_pages = (size + TMPFS_PAGE_SIZE - 1) / TMPFS_PAGE_SIZE;
  prev = 0;
  cur = node->pages;
  while(cur){
    struct tmpfs_page *next;

    next = cur->next;
    if(cur->index >= need_pages){
      if(prev)
        prev->next = next;
      else
        node->pages = next;
      if(cur->data)
        kfree(cur->data);
      kfree((char*)cur);
    } else {
      prev = cur;
    }
    cur = next;
  }
  node->size = size;
  if(old_size > size)
    md->used_bytes -= (old_size - size);
  return 0;
}

static struct tmpfs_page*
tmpfs_page_lookup(struct tmpfs_node *node, uint index)
{
  struct tmpfs_page *page;

  if(node == 0)
    return 0;
  for(page = node->pages; page; page = page->next){
    if(page->index == index)
      return page;
  }
  return 0;
}

static int
tmpfs_page_get(struct tmpfs_node *node, uint index, struct tmpfs_page **out)
{
  struct tmpfs_page *page;

  if(out)
    *out = 0;
  if(node == 0)
    return -1;

  page = tmpfs_page_lookup(node, index);
  if(page){
    if(out)
      *out = page;
    return 0;
  }

  page = (struct tmpfs_page*)kalloc();
  if(page == 0)
    return -1;
  memset(page, 0, sizeof(*page));
  page->data = kalloc();
  if(page->data == 0){
    kfree((char*)page);
    return -1;
  }
  memset(page->data, 0, TMPFS_PAGE_SIZE);
  page->index = index;
  page->next = node->pages;
  node->pages = page;
  if(out)
    *out = page;
  return 0;
}

static struct tmpfs_node*
tmpfs_find_node(struct tmpfs_node *node, uint inum)
{
  struct tmpfs_dirent *de;

  if(node == 0)
    return 0;
  if(node->inum == inum)
    return node;
  if(node->type != T_DIR)
    return 0;

  for(de = node->children; de; de = de->next){
    struct tmpfs_node *found;

    found = tmpfs_find_node(de->node, inum);
    if(found)
      return found;
  }
  return 0;
}

static int
tmpfs_parse_size(const char *data, uint *out)
{
  uint value;
  int i;

  if(data == 0 || out == 0)
    return -1;

  value = 0;
  if(strncmp(data, "size=", 5) != 0)
    return -1;
  data += 5;
  if(*data == 0)
    return -1;

  for(i = 0; data[i]; i++){
    char c = data[i];
    if(c < '0' || c > '9')
      return -1;
    value = value * 10 + (c - '0');
  }

  if(value == 0)
    return -1;
  *out = value;
  return 0;
}

static struct inode*
tmpfs_root_inode(struct vfs *fs)
{
  struct tmpfs_mount_data *md;

  if(fs == 0)
    return 0;
  md = (struct tmpfs_mount_data*)fs->fs_data;
  if(md == 0 || md->root == 0)
    return 0;

  return tmpfs_make_inode(md, md->root);
}

static struct inode*
tmpfs_walk(struct vfs *fs, char *path, int nameiparent, char *name)
{
  struct tmpfs_mount_data *md;
  struct tmpfs_node *node;
  int i;

  if(fs == 0 || path == 0)
    return 0;

  md = (struct tmpfs_mount_data*)fs->fs_data;
  if(md == 0 || md->root == 0)
    return 0;

  node = md->root;

  if(path[0] == '/')
    path++;

  while(*path){
    char elem[TMPFS_NAME_MAX + 1];
    struct tmpfs_dirent *de;
    char *rest;

    while(*path == '/')
      path++;
    if(*path == 0)
      break;

    for(i = 0; i < TMPFS_NAME_MAX && path[i] && path[i] != '/'; i++)
      elem[i] = path[i];
    if(path[i] && path[i] != '/'){
      return 0;
    }
    elem[i] = 0;

    while(path[i] && path[i] != '/')
      i++;
    rest = path + i;
    path += i;

    if(nameiparent){
      while(*rest == '/')
        rest++;
      if(*rest == 0){
        if(name)
          safestrcpy(name, elem, TMPFS_NAME_MAX + 1);
        return tmpfs_make_inode(md, node);
      }
    }

    if(strcmp(elem, ".") == 0)
      continue;
    if(strcmp(elem, "..") == 0){
      if(node->parent)
        node = node->parent;
      continue;
    }

    if(node->type != T_DIR)
      return 0;

    de = tmpfs_dirent_lookup(node, elem);
    if(de == 0)
      return 0;

    node = de->node;
  }

  if(nameiparent){
    if(name)
      name[0] = 0;
    return 0;
  }

  return tmpfs_make_inode(md, node);
}

static struct inode*
tmpfs_namei(struct vfs *fs, char *path)
{
  return tmpfs_walk(fs, path, 0, 0);
}

static struct inode*
tmpfs_nameiparent(struct vfs *fs, char *path, char *name)
{
  return tmpfs_walk(fs, path, 1, name);
}

static void
tmpfs_inode_put(struct inode *ip)
{
  iput(ip);
}

static int
tmpfs_read(struct inode *ip, char *dst, uint64_t off, uint n)
{
  struct tmpfs_node *node;
  struct dirent de;
  struct tmpfs_dirent *cur;
  uint idx;
  struct proc *p;

  if(ip == 0 || dst == 0)
    return -1;

  node = tmpfs_inode_node(ip);
  if(node == 0)
    return -1;

  if(node->type == T_DIR){
    if(n != sizeof(struct dirent))
      return -1;
    if((off % sizeof(struct dirent)) != 0)
      return -1;

    idx = off / sizeof(struct dirent);
    cur = node->children;
    while(cur && idx > 0){
      cur = cur->next;
      idx--;
    }
    if(cur == 0)
      return 0;

    de.inum = (ushort)cur->node->inum;
    safestrcpy(de.name, cur->name, sizeof(de.name));
    if((uint)dst < KERNBASE){
      p = myproc();
      if(p == 0 || p->pgdir == 0 || copyout(p->pgdir, (uint)dst, &de, sizeof(de)) < 0)
        return -1;
    } else {
      memmove(dst, &de, sizeof(de));
    }
    return sizeof(de);
  }

  if(node->type == T_FILE)
    return tmpfs_read_pages(node, dst, off, n);

  return -1;
}

static int
tmpfs_write(struct inode *ip, char *src, uint64_t off, uint n)
{
  struct tmpfs_node *node;
  struct tmpfs_mount_data *md;
  int rc;

  if(ip == 0 || src == 0)
    return -1;

  node = tmpfs_inode_node(ip);
  if(node == 0 || node->type != T_FILE)
    return -1;

  md = tmpfs_data_for_dev(ip->dev);
  if(md == 0)
    return -1;

  rc = tmpfs_write_pages(md, node, src, off, n);
  if(rc >= 0)
    ip->size = node->size;
  return rc;
}

static int
tmpfs_truncate(struct inode *ip)
{
  struct tmpfs_node *node;
  struct tmpfs_mount_data *md;

  if(ip == 0)
    return -1;

  node = tmpfs_inode_node(ip);
  if(node == 0 || node->type != T_FILE)
    return -1;

  md = tmpfs_data_for_dev(ip->dev);
  if(md == 0)
    return -1;

  if(tmpfs_truncate_node(md, node, 0) < 0)
    return -1;
  ip->size = 0;
  return 0;
}

static int
tmpfs_drop(struct inode *ip)
{
  struct tmpfs_mount_data *md;
  struct tmpfs_node *node;

  if(ip == 0)
    return -1;

  md = tmpfs_data_for_dev(ip->dev);
  node = tmpfs_inode_node(ip);
  if(md == 0 || node == 0)
    return -1;

  if(node->type == T_FILE || node->type == T_SYMLINK){
    if(node->size > 0 && md->used_bytes >= node->size)
      md->used_bytes -= node->size;
  }

  tmpfs_free_node(md, node);
  ip->addrs[0] = 0;
  return 0;
}

static int
tmpfs_stat(struct inode *ip, struct stat *st)
{
  if(ip == 0 || st == 0)
    return -1;
  stati(ip, st);
  return 0;
}

static int
tmpfs_setattr(struct inode *ip,
              int set_mode, int mode,
              int set_uid, int uid,
              int set_gid, int gid)
{
  struct tmpfs_node *node;

  if(ip == 0)
    return -1;

  node = tmpfs_inode_node(ip);
  if(node == 0)
    return -1;

  if(set_mode)
    ip->mode = (ip->mode & M_IFMT) | (mode & 07777);
  if(set_uid)
    ip->uid = uid;
  if(set_gid)
    ip->gid = gid;

  node->mode = ip->mode;
  node->uid = ip->uid;
  node->gid = ip->gid;
  return 0;
}

static int
tmpfs_access(struct inode *ip, int mode)
{
  return iaccess(ip, mode);
}

static struct inode*
tmpfs_dirlookup(struct inode *dp, char *name, uint *poff)
{
  struct tmpfs_node *dir;
  struct tmpfs_dirent *de;
  struct tmpfs_mount_data *md;
  uint idx;

  if(dp == 0 || name == 0)
    return 0;

  dir = tmpfs_inode_node(dp);
  if(dir == 0 || dir->type != T_DIR)
    return 0;

  if(strcmp(name, ".") == 0)
    return idup(dp);
  if(strcmp(name, "..") == 0){
    struct tmpfs_node *parent;
    parent = dir->parent ? dir->parent : dir;
    md = tmpfs_data_for_dev(dp->dev);
    return tmpfs_make_inode(md, parent);
  }

  de = tmpfs_dirent_lookup(dir, name);
  if(de == 0)
    return 0;

  if(poff){
    idx = 2;
    {
      struct tmpfs_dirent *cur;
      for(cur = dir->children; cur; cur = cur->next){
        if(cur == de)
          break;
        idx++;
      }
    }
    *poff = idx * sizeof(struct dirent);
  }

  md = tmpfs_data_for_dev(dp->dev);
  return tmpfs_make_inode(md, de->node);
}

static int
tmpfs_dirlink(struct inode *dp, char *name, uint inum)
{
  struct tmpfs_node *dir;
  struct tmpfs_node *node;
  struct tmpfs_mount_data *md;

  if(dp == 0 || name == 0)
    return -1;

  dir = tmpfs_inode_node(dp);
  if(dir == 0 || dir->type != T_DIR)
    return -1;

  md = tmpfs_data_for_dev(dp->dev);
  if(md == 0)
    return -1;

  node = tmpfs_find_node(md->root, inum);
  if(node == 0){
    return -1;
  }

  if(tmpfs_dirent_add(dir, name, node) < 0){
    return -1;
  }

  node->nlink++;
  return 0;
}

static int
tmpfs_link(struct inode *ip, struct inode *dp, char *name)
{
  struct tmpfs_node *node;
  struct tmpfs_node *dir;

  if(ip == 0 || dp == 0 || name == 0)
    return -1;

  node = tmpfs_inode_node(ip);
  dir = tmpfs_inode_node(dp);
  if(node == 0 || dir == 0 || dir->type != T_DIR)
    return -1;

  if(tmpfs_dirent_add(dir, name, node) < 0)
    return -1;

  node->nlink++;
  ip->nlink = node->nlink;
  return 0;
}

static int
tmpfs_remove(struct inode *dp, char *name)
{
  struct tmpfs_mount_data *md;
  struct tmpfs_node *dir;
  struct tmpfs_node *node;
  struct inode *ip;
  struct tmpfs_dirent *de;

  if(dp == 0 || name == 0)
    return -1;

  md = tmpfs_data_for_dev(dp->dev);
  dir = tmpfs_inode_node(dp);
  if(md == 0 || dir == 0 || dir->type != T_DIR)
    return -1;

  de = tmpfs_dirent_lookup(dir, name);
  if(de == 0)
    return -1;
  node = de->node;
  if(node->type == T_DIR && !tmpfs_dir_empty(node))
    return -1;

  if(tmpfs_dirent_remove(dir, name, &node) < 0)
    return -1;

  node->nlink--;
  ip = tmpfs_make_inode(md, node);
  if(ip){
    ip->nlink = node->nlink;
    if(node->nlink == 0)
      ip->valid = 1;
    iput(ip);
  }
  return 0;
}

static int
tmpfs_rename(struct inode *olddp, char *oldname,
             struct inode *newdp, char *newname)
{
  struct tmpfs_mount_data *md;
  struct tmpfs_node *srcdir;
  struct tmpfs_node *dstdir;
  struct tmpfs_node *node;
  struct tmpfs_dirent *exist;
  struct tmpfs_node *replace;

  if(olddp == 0 || newdp == 0 || oldname == 0 || newname == 0)
    return -1;
  if(olddp == newdp && strcmp(oldname, newname) == 0)
    return 0;

  srcdir = tmpfs_inode_node(olddp);
  dstdir = tmpfs_inode_node(newdp);
  if(srcdir == 0 || dstdir == 0)
    return -1;

  md = tmpfs_data_for_dev(newdp->dev);
  if(md == 0)
    return -1;

  if(tmpfs_dirent_remove(srcdir, oldname, &node) < 0)
    return -1;

  exist = tmpfs_dirent_lookup(dstdir, newname);
  if(exist){
    if(exist->node->type == T_DIR || node->type == T_DIR){
      tmpfs_dirent_add(srcdir, oldname, node);
      return -1;
    }
    replace = exist->node;
    tmpfs_dirent_remove(dstdir, newname, &replace);
    if(replace){
      struct inode *rip;

      replace->nlink--;
      rip = tmpfs_make_inode(md, replace);
      if(rip){
        rip->nlink = replace->nlink;
        if(replace->nlink == 0)
          rip->valid = 1;
        iput(rip);
      }
    }
  }

  if(tmpfs_dirent_add(dstdir, newname, node) < 0){
    tmpfs_dirent_add(srcdir, oldname, node);
    return -1;
  }

  node->parent = dstdir;
  return 0;
}

static struct inode*
tmpfs_create(struct inode *dp, char *name, short type,
             short major, short minor, int mode, int uid, int gid)
{
  struct tmpfs_mount_data *md;
  struct tmpfs_node *dir;
  struct tmpfs_node *node;
  struct inode *ip;

  (void)uid;
  (void)gid;

  if(dp == 0 || name == 0)
    return 0;
  if(type != T_FILE && type != T_DIR && type != T_SYMLINK && type != T_DEV)
    return 0;

  md = tmpfs_data_for_dev(dp->dev);
  if(md == 0)
    return 0;

  dir = tmpfs_inode_node(dp);
  if(dir == 0 || dir->type != T_DIR)
    return 0;

  node = tmpfs_alloc_node(md, type, dir);
  if(node == 0)
    return 0;
  if(mode)
    node->mode = mode & 07777;
  if(type == T_DEV){
    node->major = major;
    node->minor = minor;
    node->mode = (mode & M_IFMT) | (mode & 07777);
  }

  if(tmpfs_dirent_add(dir, name, node) < 0){
    tmpfs_free_node(md, node);
    return 0;
  }

  ip = tmpfs_make_inode(md, node);
  if(ip == 0)
    return 0;
  ilock(ip);
  return ip;
}

static int
tmpfs_readlink(struct inode *ip, char *buf, uint size)
{
  struct tmpfs_node *node;
  uint n;

  if(ip == 0 || buf == 0)
    return -1;

  node = tmpfs_inode_node(ip);
  if(node == 0 || node->type != T_SYMLINK)
    return -1;

  n = node->symlink_len;
  if(n > size)
    n = size;
  memmove(buf, node->symlink, n);
  return n;
}

static int
tmpfs_symlink(struct inode *dp, char *name, char *target)
{
  struct tmpfs_mount_data *md;
  struct tmpfs_node *dir;
  struct tmpfs_node *node;
  uint len;

  if(dp == 0 || name == 0 || target == 0)
    return -1;

  md = tmpfs_data_for_dev(dp->dev);
  if(md == 0)
    return -1;

  dir = tmpfs_inode_node(dp);
  if(dir == 0 || dir->type != T_DIR)
    return -1;

  len = strlen(target);
  if(len == 0)
    return -1;
  if(md->used_bytes + len > md->max_bytes)
    return -1;

  node = tmpfs_alloc_node(md, T_SYMLINK, dir);
  if(node == 0)
    return -1;

  node->symlink = kalloc();
  if(node->symlink == 0){
    tmpfs_free_node(md, node);
    return -1;
  }
  if(len >= TMPFS_PAGE_SIZE)
    len = TMPFS_PAGE_SIZE - 1;
  memmove(node->symlink, target, len);
  node->symlink[len] = 0;
  node->symlink_len = len;
  node->size = len;
  md->used_bytes += len;

  if(tmpfs_dirent_add(dir, name, node) < 0){
    md->used_bytes -= len;
    tmpfs_free_node(md, node);
    return -1;
  }

  return 0;
}

static int
tmpfs_mount_init(struct mount *m)
{
  struct tmpfs_mount_data *md;
  uint size;

  if(m == 0)
    return -1;
  if(m->data == 0 || m->datalen <= 0)
    return -1;

  if(tmpfs_parse_size((const char*)m->data, &size) < 0)
    return -1;

  md = (struct tmpfs_mount_data*)kalloc();
  if(md == 0)
    return -1;
  memset(md, 0, sizeof(*md));
  initlock(&md->lock, "tmpfs");
  lockdep_set_rank(&md->lock, LOCK_RANK_DEFAULT, "tmpfs");
  md->max_bytes = size;
  md->used_bytes = 0;
  md->next_inum = 1;
  md->dev = m->dev;
  md->root = tmpfs_alloc_node(md, T_DIR, 0);
  if(md->root == 0){
    kfree((char*)md);
    return -1;
  }
  md->root->parent = md->root;

  m->fs_data = md;
  return 0;
}

static void
tmpfs_destroy_node(struct tmpfs_mount_data *md, struct tmpfs_node *node)
{
  struct tmpfs_dirent *de;
  struct tmpfs_dirent *next;

  if(node == 0)
    return;

  if(node->type == T_DIR){
    de = node->children;
    while(de){
      next = de->next;
      tmpfs_destroy_node(md, de->node);
      kfree((char*)de);
      de = next;
    }
  }

  tmpfs_free_node(md, node);
}

static void
tmpfs_destroy(struct vfs *fs)
{
  struct tmpfs_mount_data *md;

  if(fs == 0)
    return;

  md = (struct tmpfs_mount_data*)fs->fs_data;
  if(md == 0)
    return;

  tmpfs_destroy_node(md, md->root);
  md->root = 0;
}

void
vfs_tmpfs_init(struct vfs *fs)
{
  if(fs == 0)
    return;

  safestrcpy(fs->name, "tmpfs", sizeof(fs->name));
  fs->caps = VFS_CAP_READ | VFS_CAP_WRITE | VFS_CAP_CREATE |
             VFS_CAP_REMOVE | VFS_CAP_LINK | VFS_CAP_MKDIR |
             VFS_CAP_RENAME | VFS_CAP_SYMLINK;
  fs->fs_data = 0;
  fs->fs_destroy = tmpfs_destroy;
  fs->mount_init = tmpfs_mount_init;
  fs->ops.root_inode = tmpfs_root_inode;
  fs->ops.namei = tmpfs_namei;
  fs->ops.nameiparent = tmpfs_nameiparent;
  fs->ops.inode_put = tmpfs_inode_put;
  fs->vnode_ops.read = tmpfs_read;
  fs->vnode_ops.write = tmpfs_write;
  fs->vnode_ops.truncate = tmpfs_truncate;
  fs->vnode_ops.drop = tmpfs_drop;
  fs->vnode_ops.stat = tmpfs_stat;
  fs->vnode_ops.setattr = tmpfs_setattr;
  fs->vnode_ops.access = tmpfs_access;
  fs->vnode_ops.dirlookup = tmpfs_dirlookup;
  fs->vnode_ops.dirlink = tmpfs_dirlink;
  fs->vnode_ops.link = tmpfs_link;
  fs->vnode_ops.remove = tmpfs_remove;
  fs->vnode_ops.rename = tmpfs_rename;
  fs->vnode_ops.create = tmpfs_create;
  fs->vnode_ops.readlink = tmpfs_readlink;
  fs->vnode_ops.symlink = tmpfs_symlink;
}
