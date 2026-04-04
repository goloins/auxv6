#include "types.h"
#include "defs.h"
#include "param.h"
#include "mmu.h"
#include "spinlock.h"
#include "sleeplock.h"
#include "vfs.h"
#include "fs.h"
#include "file.h"
#include "stat.h"
#include "buf.h"

#define UFS2_SUPERBLOCK_OFFSET 65536U
#define UFS2_SUPERBLOCK_READ   8192U
#define UFS2_MAGIC             0x19540119U
#define UFS2_ROOT_INO          2U

/* FreeBSD/OpenBSD UFS2 superblock field offsets (bytes from superblock start). */
#define UFS2_SB_BSIZE_OFF      48U
#define UFS2_SB_FSIZE_OFF      52U
#define UFS2_SB_IBLKNO_OFF     24U
#define UFS2_SB_IPG_OFF        184U
#define UFS2_SB_FPG_OFF        188U
#define UFS2_SB_INOPB_OFF      104U
#define UFS2_SB_MAGIC_OFF      1372U

#define UFS2_MAX_NAME          255
#define UFS2_NDADDR            12
#define UFS2_NIADDR            3

#define UFS2_IP_INUM_LO        0
#define UFS2_IP_INUM_HI        1

struct ufs2_dinode {
  ushort di_mode;
  short di_nlink;
  uint di_uid;
  uint di_gid;
  uint di_blksize;
  uint di_pad0;
  uint64 di_size;
  uint64 di_blocks;
  uint64 di_atime;
  uint di_atimensec;
  uint di_mtime;
  uint di_mtimensec;
  uint di_ctime;
  uint di_ctimensec;
  uint di_birthtime;
  uint di_birthnsec;
  uint di_gen;
  uint di_kernflags;
  uint di_flags;
  uint di_extsize;
  uint64 di_extb[2];
  uint64 di_db[UFS2_NDADDR];
  uint64 di_ib[UFS2_NIADDR];
  uint64 di_modrev;
  uint32 di_freelink;
  uint32 di_spare[2];
};

struct ufs2_dirent {
  uint d_ino;
  ushort d_reclen;
  uchar d_type;
  uchar d_namlen;
  char d_name[1];
} __attribute__((packed));

struct ufs2_mount_data {
  int dev;
  uint bsize;
  uint fsize;
  uint ipg;
  uint fpg;
  uint inopb;
  uint64 iblkno;
};

static struct ufs2_mount_data* ufs2_data_for_dev(uint dev);
static int ufs2_read_dinode(struct ufs2_mount_data *md, uint inum,
                            struct ufs2_dinode *dip);
static struct inode* ufs2_make_inode(struct ufs2_mount_data *md, uint inum);
static int ufs2_read_data(struct ufs2_mount_data *md, struct ufs2_dinode *dip,
                          char *dst, uint off, uint n);
static struct inode* ufs2_dirlookup(struct inode *dp, char *name, uint *poff);

static uint
ufs2_min_u32(uint a, uint b)
{
  return (a < b) ? a : b;
}

static uint
ufs2_get_u32(const uchar *base, uint off)
{
  uint v;

  memmove(&v, base + off, sizeof(v));
  return v;
}

static uint64
ufs2_get_u64(const uchar *base, uint off)
{
  uint64 v;

  memmove(&v, base + off, sizeof(v));
  return v;
}

static struct ufs2_mount_data*
ufs2_data_for_dev(uint dev)
{
  return (struct ufs2_mount_data*)vfs_dev_fs_data(dev);
}

static int
ufs2_dev_read(uint dev, uint off, char *dst, uint n)
{
  uint done;

  done = 0;
  while(done < n){
    uint cur;
    uint blockno;
    uint boff;
    uint take;
    struct buf *b;

    cur = off + done;
    blockno = cur / BSIZE;
    boff = cur % BSIZE;
    take = ufs2_min_u32(BSIZE - boff, n - done);

    if(bread_ok(dev, blockno, &b) < 0)
      return -1;
    memmove(dst + done, (char*)b->data + boff, take);
    brelse(b);
    done += take;
  }

  return 0;
}

static uint
ufs2_inode_type(ushort mode)
{
  switch(mode & M_IFMT){
  case M_IFDIR:
    return T_DIR;
  case M_IFLNK:
    return T_SYMLINK;
  case M_IFCHR:
  case M_IFBLK:
    return T_DEV;
  default:
    return T_FILE;
  }
}

static int
ufs2_inode_offset(struct ufs2_mount_data *md, uint inum, uint64 *out)
{
  uint cg;
  uint idx;
  uint frag;
  uint64 fragno;

  if(md == 0 || out == 0 || inum == 0)
    return -1;
  if(md->ipg == 0 || md->inopb == 0 || md->fsize == 0)
    return -1;

  cg = (inum - 1) / md->ipg;
  idx = (inum - 1) % md->ipg;
  frag = idx / md->inopb;
  fragno = md->iblkno + (uint64)cg * md->fpg + frag;
  *out = fragno * md->fsize + (uint64)(idx % md->inopb) * sizeof(struct ufs2_dinode);
  return 0;
}

static int
ufs2_read_dinode(struct ufs2_mount_data *md, uint inum, struct ufs2_dinode *dip)
{
  uint64 off;

  if(md == 0 || dip == 0)
    return -1;
  if(ufs2_inode_offset(md, inum, &off) < 0)
    return -1;
  if(off > 0xFFFFFFFFULL)
    return -1;
  if(ufs2_dev_read(md->dev, (uint)off, (char*)dip, sizeof(*dip)) < 0)
    return -1;
  return 0;
}

static int
ufs2_lbn_to_blkno(struct ufs2_mount_data *md, struct ufs2_dinode *dip,
                  uint lbn, uint64 *blkno)
{
  uint64 indbuf[128];

  if(md == 0 || dip == 0 || blkno == 0)
    return -1;

  if(lbn < UFS2_NDADDR){
    *blkno = dip->di_db[lbn];
    return 0;
  }

  lbn -= UFS2_NDADDR;
  if(lbn >= md->bsize / sizeof(uint64))
    return -1;
  if(dip->di_ib[0] == 0){
    *blkno = 0;
    return 0;
  }

  if(md->bsize > sizeof(indbuf))
    return -1;
  if(dip->di_ib[0] > 0xFFFFFFFFULL)
    return -1;
  if(ufs2_dev_read(md->dev, (uint)(dip->di_ib[0] * md->fsize),
                   (char*)indbuf, md->bsize) < 0)
    return -1;

  *blkno = indbuf[lbn];
  return 0;
}

static int
ufs2_read_data(struct ufs2_mount_data *md, struct ufs2_dinode *dip,
               char *dst, uint off, uint n)
{
  uint done;

  if(md == 0 || dip == 0 || dst == 0)
    return -1;
  if(n == 0)
    return 0;

  done = 0;
  while(done < n){
    uint cur;
    uint lbn;
    uint boff;
    uint take;
    uint64 blkno;
    uint64 byteoff;

    cur = off + done;
    lbn = cur / md->bsize;
    boff = cur % md->bsize;
    take = ufs2_min_u32(md->bsize - boff, n - done);

    if(ufs2_lbn_to_blkno(md, dip, lbn, &blkno) < 0)
      return (done > 0) ? (int)done : -1;
    if(blkno == 0){
      memset(dst + done, 0, take);
      done += take;
      continue;
    }
    byteoff = blkno * md->fsize + boff;
    if(byteoff > 0xFFFFFFFFULL)
      return (done > 0) ? (int)done : -1;
    if(ufs2_dev_read(md->dev, (uint)byteoff, dst + done, take) < 0)
      return (done > 0) ? (int)done : -1;

    done += take;
  }

  return (int)done;
}

static struct inode*
ufs2_make_inode(struct ufs2_mount_data *md, uint inum)
{
  struct inode *ip;
  struct ufs2_dinode dip;
  uint64 lo;
  uint64 hi;

  if(md == 0)
    return 0;
  if(ufs2_read_dinode(md, inum, &dip) < 0)
    return 0;

  ip = iget(md->dev, inum);
  if(ip == 0)
    return 0;

  acquiresleep(&ip->lock);
  ip->dev = md->dev;
  ip->inum = inum;
  ip->valid = 1;
  ip->type = ufs2_inode_type(dip.di_mode);
  ip->nlink = dip.di_nlink;
  ip->uid = (short)(dip.di_uid & 0xFFFF);
  ip->gid = (short)(dip.di_gid & 0xFFFF);
  ip->mode = dip.di_mode;
  ip->major = 0;
  ip->minor = 0;
  ip->size = (uint)dip.di_size;
  memset(ip->addrs, 0, sizeof(ip->addrs));
  lo = (uint64)inum;
  hi = (uint64)inum >> 32;
  ip->addrs[UFS2_IP_INUM_LO] = (uint)(lo & 0xFFFFFFFFULL);
  ip->addrs[UFS2_IP_INUM_HI] = (uint)(hi & 0xFFFFFFFFULL);
  releasesleep(&ip->lock);

  return ip;
}

static uint
ufs2_inode_from_ip(struct inode *ip)
{
  uint64 inum;

  inum = (uint64)ip->addrs[UFS2_IP_INUM_LO] |
         ((uint64)ip->addrs[UFS2_IP_INUM_HI] << 32);
  if(inum == 0)
    inum = ip->inum;
  return (uint)inum;
}

static int
ufs2_name_equal(char *want, char *got, uint gotlen)
{
  uint i;

  for(i = 0; i < gotlen; i++){
    if(want[i] == 0)
      return 0;
    if(want[i] != got[i])
      return 0;
  }
  return want[gotlen] == 0;
}

static int
ufs2_dirent_valid(struct ufs2_dirent *de, uint remain)
{
  if(de == 0)
    return 0;
  if(de->d_reclen < 8)
    return 0;
  if(de->d_reclen > remain)
    return 0;
  if(de->d_namlen == 0 || de->d_namlen > UFS2_MAX_NAME)
    return 0;
  if((uint)de->d_namlen + 8 > de->d_reclen)
    return 0;
  return 1;
}

static struct inode*
ufs2_dirlookup(struct inode *dp, char *name, uint *poff)
{
  struct ufs2_mount_data *md;
  struct ufs2_dinode ddip;
  char *buf;
  uint off;

  if(dp == 0 || name == 0)
    return 0;
  if(dp->type != T_DIR)
    return 0;

  md = ufs2_data_for_dev(dp->dev);
  if(md == 0)
    return 0;
  if(ufs2_read_dinode(md, ufs2_inode_from_ip(dp), &ddip) < 0)
    return 0;

  buf = kalloc();
  if(buf == 0)
    return 0;

  off = 0;
  while(off < (uint)ddip.di_size){
    struct ufs2_dirent *de;
    uint chunk;

    chunk = ufs2_min_u32(PGSIZE, (uint)ddip.di_size - off);
    if(ufs2_read_data(md, &ddip, buf, off, chunk) < 0)
      break;

    {
      uint p;
      p = 0;
      while(p + 8 <= chunk){
        de = (struct ufs2_dirent*)(buf + p);
        if(!ufs2_dirent_valid(de, chunk - p))
          break;
        if(de->d_ino != 0 && ufs2_name_equal(name, de->d_name, de->d_namlen)){
          struct inode *ip;
          if(poff)
            *poff = off + p;
          ip = ufs2_make_inode(md, de->d_ino);
          kfree(buf);
          return ip;
        }
        p += de->d_reclen;
      }
    }

    off += chunk;
  }

  kfree(buf);
  return 0;
}

static struct inode*
ufs2_root_inode(struct vfs *fs)
{
  struct ufs2_mount_data *md;

  if(fs == 0)
    return 0;
  md = (struct ufs2_mount_data*)fs->fs_data;
  if(md == 0)
    return 0;
  return ufs2_make_inode(md, UFS2_ROOT_INO);
}

static int
ufs2_read(struct inode *ip, char *dst, uint off, uint n)
{
  struct ufs2_mount_data *md;
  struct ufs2_dinode dip;
  uint inum;

  if(ip == 0 || dst == 0)
    return -1;

  md = ufs2_data_for_dev(ip->dev);
  if(md == 0)
    return -1;

  inum = ufs2_inode_from_ip(ip);
  if(ufs2_read_dinode(md, inum, &dip) < 0)
    return -1;

  if(off >= dip.di_size)
    return 0;
  if(off + n > dip.di_size)
    n = (uint)(dip.di_size - off);

  if(ip->type != T_DIR)
    return ufs2_read_data(md, &dip, dst, off, n);

  {
    uint target;
    uint emitted;
    uint pos;
    struct dirent out;
    char *dirbuf;

    target = off / sizeof(struct dirent);
    emitted = 0;
    pos = 0;
    dirbuf = kalloc();
    if(dirbuf == 0)
      return -1;

    while(pos < (uint)dip.di_size && emitted + sizeof(struct dirent) <= n){
      uint chunk;
      uint p;

      chunk = ufs2_min_u32(PGSIZE, (uint)dip.di_size - pos);
      if(ufs2_read_data(md, &dip, dirbuf, pos, chunk) < 0)
        break;

      p = 0;
      while(p + 8 <= chunk && emitted + sizeof(struct dirent) <= n){
        struct ufs2_dirent *de;
        uint i;

        de = (struct ufs2_dirent*)(dirbuf + p);
        if(!ufs2_dirent_valid(de, chunk - p))
          break;
        if(de->d_ino != 0){
          if(target == 0){
            memset(&out, 0, sizeof(out));
            out.inum = (ushort)(de->d_ino & 0xFFFF);
            for(i = 0; i < de->d_namlen && i < DIRSIZ - 1; i++)
              out.name[i] = de->d_name[i];
            out.name[i] = 0;
            memmove(dst + emitted, &out, sizeof(out));
            emitted += sizeof(out);
          } else {
            target--;
          }
        }
        p += de->d_reclen;
      }

      pos += chunk;
    }

    kfree(dirbuf);
    return emitted;
  }
}

static int
ufs2_write(struct inode *ip, char *src, uint off, uint n)
{
  (void)ip;
  (void)src;
  (void)off;
  (void)n;
  return -1;
}

static int
ufs2_truncate(struct inode *ip)
{
  (void)ip;
  return -1;
}

static int
ufs2_stat(struct inode *ip, struct stat *st)
{
  if(ip == 0 || st == 0)
    return -1;

  st->st_type = ip->type;
  st->st_dev = ip->dev;
  st->st_ino = ip->inum;
  st->st_major = ip->major;
  st->st_minor = ip->minor;
  st->st_nlink = ip->nlink;
  st->st_uid = ip->uid;
  st->st_gid = ip->gid;
  st->st_mode = ip->mode;
  st->st_size = ip->size;
  st->st_atime = 0;
  st->st_mtime = 0;
  st->st_ctime = 0;
  return 0;
}

static int
ufs2_setattr(struct inode *ip, int set_mode, int mode,
             int set_uid, int uid, int set_gid, int gid)
{
  (void)ip;
  (void)set_mode;
  (void)mode;
  (void)set_uid;
  (void)uid;
  (void)set_gid;
  (void)gid;
  return -1;
}

static int
ufs2_access(struct inode *ip, int mode)
{
  (void)ip;
  (void)mode;
  return 0;
}

static int
ufs2_readlink(struct inode *ip, char *buf, uint size)
{
  struct ufs2_mount_data *md;
  struct ufs2_dinode dip;
  uint inum;
  uint n;

  if(ip == 0 || buf == 0 || size == 0)
    return -1;
  if(ip->type != T_SYMLINK)
    return -1;

  md = ufs2_data_for_dev(ip->dev);
  if(md == 0)
    return -1;
  inum = ufs2_inode_from_ip(ip);
  if(ufs2_read_dinode(md, inum, &dip) < 0)
    return -1;

  n = (uint)dip.di_size;
  if(n >= size)
    n = size - 1;

  if(dip.di_size <= sizeof(dip.di_db) + sizeof(dip.di_ib)){
    memmove(buf, (char*)dip.di_db, n);
    buf[n] = 0;
    return n;
  }

  if(ufs2_read_data(md, &dip, buf, 0, n) < 0)
    return -1;
  buf[n] = 0;
  return n;
}

static int
ufs2_dirlink(struct inode *dp, char *name, uint inum)
{
  (void)dp;
  (void)name;
  (void)inum;
  return -1;
}

static int
ufs2_link(struct inode *ip, struct inode *dp, char *name)
{
  (void)ip;
  (void)dp;
  (void)name;
  return -1;
}

static int
ufs2_remove(struct inode *dp, char *name)
{
  (void)dp;
  (void)name;
  return -1;
}

static int
ufs2_rename(struct inode *olddp, char *oldname,
            struct inode *newdp, char *newname)
{
  (void)olddp;
  (void)oldname;
  (void)newdp;
  (void)newname;
  return -1;
}

static struct inode*
ufs2_create(struct inode *dp, char *name, short type,
            short major, short minor, int mode, int uid, int gid)
{
  (void)dp;
  (void)name;
  (void)type;
  (void)major;
  (void)minor;
  (void)mode;
  (void)uid;
  (void)gid;
  return 0;
}

static char*
ufs2_skipelem(char *path, char *name)
{
  char *s;
  int len;

  while(*path == '/')
    path++;
  if(*path == 0)
    return 0;

  s = path;
  while(*path != '/' && *path != 0)
    path++;

  len = path - s;
  if(len >= DIRSIZ)
    len = DIRSIZ - 1;
  memmove(name, s, len);
  name[len] = 0;

  while(*path == '/')
    path++;
  return path;
}

static struct inode*
ufs2_walk(struct vfs *fs, char *path, int want_parent, char *name)
{
  struct inode *ip;
  struct inode *next;
  char elem[DIRSIZ];
  char *p;
  int i;

  if(fs == 0 || path == 0)
    return 0;

  ip = ufs2_root_inode(fs);
  if(ip == 0)
    return 0;

  p = path;
  while((p = ufs2_skipelem(p, elem)) != 0){
    if(elem[0] == 0 || (elem[0] == '.' && elem[1] == 0))
      continue;

    if(want_parent && *p == 0){
      if(name){
        for(i = 0; i < DIRSIZ - 1 && elem[i]; i++)
          name[i] = elem[i];
        name[i] = 0;
      }
      return ip;
    }

    ilock(ip);
    next = ufs2_dirlookup(ip, elem, 0);
    iunlock(ip);

    if(next == 0){
      iput(ip);
      return 0;
    }
    iput(ip);
    ip = next;
  }

  if(want_parent){
    iput(ip);
    return 0;
  }
  return ip;
}

static struct inode*
ufs2_namei(struct vfs *fs, char *path)
{
  return ufs2_walk(fs, path, 0, 0);
}

static struct inode*
ufs2_nameiparent(struct vfs *fs, char *path, char *name)
{
  return ufs2_walk(fs, path, 1, name);
}

static void
ufs2_inode_put(struct inode *ip)
{
  iput(ip);
}

static int
ufs2_mount_init(struct mount *m)
{
  uchar *sb;
  struct ufs2_mount_data *md;
  uint magic;

  if(m == 0)
    return -1;

  sb = (uchar*)kalloc();
  if(sb == 0)
    return -1;
  md = (struct ufs2_mount_data*)kalloc();
  if(md == 0){
    kfree((char*)sb);
    return -1;
  }
  memset(md, 0, sizeof(*md));

  if(ufs2_dev_read(m->dev, UFS2_SUPERBLOCK_OFFSET, (char*)sb, PGSIZE) < 0){
    kfree((char*)md);
    kfree((char*)sb);
    return -1;
  }

  if(UFS2_SUPERBLOCK_READ > PGSIZE){
    /* Keep analyzer happy: all currently used fields are inside first page. */
  }

  magic = ufs2_get_u32(sb, UFS2_SB_MAGIC_OFF);
  if(magic != UFS2_MAGIC){
    kfree((char*)md);
    kfree((char*)sb);
    return -1;
  }

  md->dev = m->dev;
  md->iblkno = ufs2_get_u64(sb, UFS2_SB_IBLKNO_OFF);
  md->bsize = ufs2_get_u32(sb, UFS2_SB_BSIZE_OFF);
  md->fsize = ufs2_get_u32(sb, UFS2_SB_FSIZE_OFF);
  md->ipg = ufs2_get_u32(sb, UFS2_SB_IPG_OFF);
  md->fpg = ufs2_get_u32(sb, UFS2_SB_FPG_OFF);
  md->inopb = ufs2_get_u32(sb, UFS2_SB_INOPB_OFF);

  if(md->bsize == 0 || md->fsize == 0 || md->ipg == 0 ||
     md->fpg == 0 || md->inopb == 0 || md->iblkno == 0 ||
     (md->bsize % md->fsize) != 0 || md->bsize > 65536){
    kfree((char*)md);
    kfree((char*)sb);
    return -1;
  }

  MOUNTDBG("ufs2: mounted dev=%d bsize=%d fsize=%d ipg=%d fpg=%d inopb=%d\n",
           md->dev, md->bsize, md->fsize, md->ipg, md->fpg, md->inopb);

  m->fs_data = md;
  kfree((char*)sb);
  return 0;
}

static void
ufs2_fs_destroy(struct vfs *fs)
{
  if(fs == 0)
    return;
  if(fs->fs_data){
    kfree(fs->fs_data);
    fs->fs_data = 0;
  }
}

void
vfs_ufs2_init(struct vfs *fs)
{
  if(fs == 0)
    return;

  safestrcpy(fs->name, "ufs2", VFS_NAME_MAX);
  fs->caps = VFS_CAP_READ;
  fs->fs_data = 0;
  fs->fs_destroy = ufs2_fs_destroy;
  fs->mount_init = ufs2_mount_init;

  fs->ops.root_inode = ufs2_root_inode;
  fs->ops.namei = ufs2_namei;
  fs->ops.nameiparent = ufs2_nameiparent;
  fs->ops.inode_put = ufs2_inode_put;

  fs->vnode_ops.read = ufs2_read;
  fs->vnode_ops.write = ufs2_write;
  fs->vnode_ops.truncate = ufs2_truncate;
  fs->vnode_ops.drop = 0;
  fs->vnode_ops.stat = ufs2_stat;
  fs->vnode_ops.setattr = ufs2_setattr;
  fs->vnode_ops.access = ufs2_access;
  fs->vnode_ops.dirlookup = ufs2_dirlookup;
  fs->vnode_ops.dirlink = ufs2_dirlink;
  fs->vnode_ops.link = ufs2_link;
  fs->vnode_ops.remove = ufs2_remove;
  fs->vnode_ops.rename = ufs2_rename;
  fs->vnode_ops.faultctl = 0;
  fs->vnode_ops.create = ufs2_create;
  fs->vnode_ops.readlink = ufs2_readlink;
  fs->vnode_ops.symlink = 0;
}