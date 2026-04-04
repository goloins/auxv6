#include "types.h"
#include "defs.h"
#include "param.h"
#include "stat.h"
#include "spinlock.h"
#include "sleeplock.h"
#include "fs.h"
#include "file.h"
#include "buf.h"
#include "vfs.h"
#include "stdint.h"

#define EXFAT_BOOT_SIG                0xAA55
#define EXFAT_CLUSTER_FIRST           2U
#define EXFAT_FAT_EOC_MIN             0xFFFFFFF8U

#define EXFAT_ENTRY_FILE              0x85
#define EXFAT_ENTRY_STREAM            0xC0
#define EXFAT_ENTRY_NAME              0xC1

#define EXFAT_ATTR_RDONLY             0x0001
#define EXFAT_ATTR_DIR                0x0010

#define EXFAT_STREAM_FLAG_NOFATCHAIN  0x02

#define EXFAT_INUM_ROOT               ROOTINO

struct exfat_boot_sector {
  uchar jump[3];
  char fs_name[8];
  uchar must_be_zero[53];
  uint64_t partition_offset;
  uint64_t volume_length;
  uint fat_offset;
  uint fat_length;
  uint cluster_heap_offset;
  uint cluster_count;
  uint root_dir_cluster;
  uint volume_serial;
  ushort fs_revision;
  ushort volume_flags;
  uchar bytes_per_sector_shift;
  uchar sectors_per_cluster_shift;
  uchar num_fats;
  uchar drive_select;
  uchar percent_in_use;
  uchar reserved[7];
  uchar boot_code[390];
  ushort boot_signature;
} __attribute__((packed));

struct exfat_mount_data {
  int dev;
  uint fat_offset;
  uint fat_length;
  uint cluster_heap_offset;
  uint cluster_count;
  uint root_dir_cluster;
  uint sectors_per_cluster;
  uint bytes_per_cluster;
  uint volume_flags;
  uchar num_fats;
};

struct exfat_file_meta {
  ushort attrs;
  uchar stream_flags;
  uint first_cluster;
  uint64_t data_length;
};

struct exfat_iter_state {
  int have_primary;
  int secondary_left;
  uint primary_index;
  ushort attrs;
  uchar stream_flags;
  uint first_cluster;
  uint64_t data_length;
  uchar name_chars_expected;
  char name[DIRSIZ];
  int name_len;
  int have_stream;
};

typedef int (*exfat_dir_visit_fn)(struct inode *dp, uint inum,
                                  struct exfat_file_meta *meta,
                                  const char *name, void *arg);

static struct exfat_mount_data* exfat_data_for_dev(uint dev);
static struct inode* exfat_root_inode(struct vfs *fs);
static struct inode* exfat_dirlookup(struct inode *dp, char *name, uint *poff);

static uint
exfat_min_u32(uint a, uint b)
{
  return (a < b) ? a : b;
}

static int
exfat_name_eq_ci(const char *a, const char *b)
{
  if(a == 0 || b == 0)
    return 0;
  while(*a && *b){
    char ca;
    char cb;

    ca = *a;
    cb = *b;
    if(ca >= 'A' && ca <= 'Z')
      ca += ('a' - 'A');
    if(cb >= 'A' && cb <= 'Z')
      cb += ('a' - 'A');
    if(ca != cb)
      return 0;
    a++;
    b++;
  }
  return *a == 0 && *b == 0;
}

static uint
exfat_visible_inum(uint inum)
{
  uint v;

  v = inum & 0xFFFFU;
  if(v == 0)
    v = 1;
  return v;
}

static uint
exfat_make_child_inum(struct inode *dp, uint primary_index)
{
  uint seed;

  seed = dp->inum ^ (dp->addrs[0] * 2654435761U);
  seed ^= (primary_index + 0x9e3779b9U + (seed << 6) + (seed >> 2));
  seed &= 0x7FFFFFFFU;
  if(seed == 0)
    seed = 2;
  return seed;
}

static struct buf*
exfat_bread(struct exfat_mount_data *md, uint sec)
{
  struct buf *b;
  uint nblocks;

  if(md == 0)
    return 0;
  nblocks = bdev_nblocks(md->dev);
  if(nblocks == 0 || sec >= nblocks)
    return 0;
  if(bread_ok(md->dev, sec, &b) < 0)
    return 0;
  return b;
}

static uint
exfat_cluster_first_sector(struct exfat_mount_data *md, uint cluster)
{
  uint idx;

  if(md == 0)
    return 0;
  if(cluster < EXFAT_CLUSTER_FIRST)
    return 0;
  if(cluster >= md->cluster_count + EXFAT_CLUSTER_FIRST)
    return 0;

  idx = cluster - EXFAT_CLUSTER_FIRST;
  return md->cluster_heap_offset + idx * md->sectors_per_cluster;
}

static int
exfat_fat_next(struct exfat_mount_data *md, uint cluster, uint *out_next)
{
  uint off;
  uint sec;
  uint soff;
  struct buf *b;
  uint v;

  if(md == 0 || out_next == 0)
    return -1;
  if(cluster < EXFAT_CLUSTER_FIRST)
    return -1;

  off = cluster * 4U;
  sec = md->fat_offset + (off / BSIZE);
  soff = off % BSIZE;
  if(soff + 4 > BSIZE)
    return -1;
  if(sec >= md->fat_offset + md->fat_length)
    return -1;

  b = exfat_bread(md, sec);
  if(b == 0)
    return -1;
  v = *(uint*)(b->data + soff) & 0x0FFFFFFFU;
  brelse(b);

  if(v < EXFAT_CLUSTER_FIRST || v >= EXFAT_FAT_EOC_MIN){
    *out_next = 0;
    return 0;
  }
  if(v >= md->cluster_count + EXFAT_CLUSTER_FIRST)
    return -1;

  *out_next = v;
  return 0;
}

static int
exfat_cluster_by_index(struct exfat_mount_data *md, uint first_cluster,
                       uchar stream_flags, uint cidx, uint *out_cluster)
{
  uint cur;
  uint i;

  if(md == 0 || out_cluster == 0)
    return -1;
  if(first_cluster < EXFAT_CLUSTER_FIRST)
    return -1;

  if(stream_flags & EXFAT_STREAM_FLAG_NOFATCHAIN){
    cur = first_cluster + cidx;
    if(cur >= md->cluster_count + EXFAT_CLUSTER_FIRST)
      return -1;
    *out_cluster = cur;
    return 0;
  }

  cur = first_cluster;
  for(i = 0; i < cidx; i++){
    uint next;

    if(exfat_fat_next(md, cur, &next) < 0)
      return -1;
    if(next == 0)
      return -1;
    cur = next;
  }

  *out_cluster = cur;
  return 0;
}

static int
exfat_stream_read(struct exfat_mount_data *md, uint first_cluster,
                  uchar stream_flags, uint64_t total_len,
                  char *dst, uint off, uint n)
{
  uint done;
  uint cluster_bytes;

  if(md == 0 || dst == 0)
    return -1;
  if(n == 0)
    return 0;
  if(total_len == 0 || first_cluster < EXFAT_CLUSTER_FIRST)
    return 0;
  if((uint64_t)off >= total_len)
    return 0;

  if((uint64_t)off + n > total_len)
    n = (uint)(total_len - (uint64_t)off);

  cluster_bytes = md->bytes_per_cluster;
  done = 0;
  while(done < n){
    uint abs_off;
    uint cidx;
    uint coff;
    uint cluster;
    uint csec;
    uint copied;
    uint need;

    abs_off = off + done;
    cidx = abs_off / cluster_bytes;
    coff = abs_off % cluster_bytes;
    need = exfat_min_u32(n - done, cluster_bytes - coff);

    if(exfat_cluster_by_index(md, first_cluster, stream_flags, cidx, &cluster) < 0)
      return (done > 0) ? (int)done : -1;
    csec = exfat_cluster_first_sector(md, cluster);
    if(csec == 0)
      return (done > 0) ? (int)done : -1;

    copied = 0;
    while(copied < need){
      uint boff;
      uint sec_idx;
      uint sec_off;
      uint take;
      struct buf *b;

      boff = coff + copied;
      sec_idx = boff / BSIZE;
      sec_off = boff % BSIZE;
      take = exfat_min_u32(need - copied, BSIZE - sec_off);

      b = exfat_bread(md, csec + sec_idx);
      if(b == 0)
        return (done > 0) ? (int)done : -1;
      memmove(dst + done + copied, b->data + sec_off, take);
      brelse(b);
      copied += take;
    }

    done += need;
  }

  return (int)done;
}

static int
exfat_skipelem(char *path, char *name)
{
  char *s;
  int len;

  while(*path == '/')
    path++;
  if(*path == 0)
    return 0;

  s = path;
  len = 0;
  while(*path != '/' && *path != 0){
    if(len < DIRSIZ)
      name[len++] = *path;
    path++;
  }
  name[len] = 0;
  while(*path == '/')
    path++;

  return (int)(path - s);
}

static void
exfat_utf16_append_ascii(char *dst, int *plen, int max,
                         const uchar *pairs, int chars,
                         int *remaining)
{
  int i;

  for(i = 0; i < chars; i++){
    ushort c;

    if(remaining && *remaining <= 0)
      return;
    c = (ushort)pairs[i * 2] | ((ushort)pairs[i * 2 + 1] << 8);
    if(c == 0)
      return;
    if(*plen < max - 1){
      if(c >= 0x20 && c <= 0x7E)
        dst[*plen] = (char)c;
      else
        dst[*plen] = '?';
      (*plen)++;
      dst[*plen] = 0;
    }
    if(remaining)
      (*remaining)--;
  }
}

static int
exfat_dir_iter(struct inode *dp, exfat_dir_visit_fn visit, void *arg)
{
  struct exfat_mount_data *md;
  struct exfat_iter_state st;
  uint cluster;
  uint entry_idx;

  if(dp == 0 || visit == 0)
    return -1;
  if(dp->type != T_DIR)
    return -1;

  md = exfat_data_for_dev(dp->dev);
  if(md == 0)
    return -1;

  memset(&st, 0, sizeof(st));
  cluster = dp->addrs[0];
  entry_idx = 0;

  while(cluster >= EXFAT_CLUSTER_FIRST){
    uint csec;
    uint sidx;

    csec = exfat_cluster_first_sector(md, cluster);
    if(csec == 0)
      return -1;

    for(sidx = 0; sidx < md->sectors_per_cluster; sidx++){
      struct buf *b;
      uint off;

      b = exfat_bread(md, csec + sidx);
      if(b == 0)
        return -1;

      for(off = 0; off + 32 <= BSIZE; off += 32){
        uchar *e;
        uchar et;
        uchar base;

        e = b->data + off;
        et = e[0];
        if(et == 0x00){
          brelse(b);
          return 0;
        }

        if((et & 0x80) == 0){
          st.have_primary = 0;
          st.secondary_left = 0;
          entry_idx++;
          continue;
        }

        base = et & 0x7F;
        if(base == (EXFAT_ENTRY_FILE & 0x7F)){
          st.have_primary = 1;
          st.secondary_left = e[1];
          st.primary_index = entry_idx;
          st.attrs = *(ushort*)(e + 4);
          st.stream_flags = 0;
          st.first_cluster = 0;
          st.data_length = 0;
          st.name_chars_expected = 0;
          st.name[0] = 0;
          st.name_len = 0;
          st.have_stream = 0;
          entry_idx++;
          continue;
        }

        if(!st.have_primary || st.secondary_left <= 0){
          entry_idx++;
          continue;
        }

        if(base == (EXFAT_ENTRY_STREAM & 0x7F)){
          st.stream_flags = e[1];
          st.name_chars_expected = e[3];
          st.first_cluster = *(uint*)(e + 20);
          st.data_length = *(uint64_t*)(e + 24);
          st.have_stream = 1;
        } else if(base == (EXFAT_ENTRY_NAME & 0x7F)){
          int remaining;

          remaining = (int)st.name_chars_expected - st.name_len;
          exfat_utf16_append_ascii(st.name, &st.name_len, sizeof(st.name),
                                   e + 2, 15, &remaining);
        }

        st.secondary_left--;
        if(st.secondary_left == 0 && st.have_stream){
          uint inum;
          struct exfat_file_meta meta;

          if(st.name[0] != 0 && (st.attrs & 0x08) == 0){
            inum = exfat_make_child_inum(dp, st.primary_index);
            meta.attrs = st.attrs;
            meta.stream_flags = st.stream_flags;
            meta.first_cluster = st.first_cluster;
            meta.data_length = st.data_length;
            if(visit(dp, inum, &meta, st.name, arg) != 0){
              brelse(b);
              return 0;
            }
          }
          st.have_primary = 0;
        }

        entry_idx++;
      }

      brelse(b);
    }

    if(dp->addrs[1] & EXFAT_STREAM_FLAG_NOFATCHAIN){
      cluster++;
      if(cluster >= md->cluster_count + EXFAT_CLUSTER_FIRST)
        break;
    } else {
      uint next;

      if(exfat_fat_next(md, cluster, &next) < 0)
        return -1;
      if(next == 0)
        break;
      cluster = next;
    }
  }

  return 0;
}

struct exfat_lookup_ctx {
  char *name;
  int found;
  struct exfat_file_meta meta;
  uint inum;
};

static int
exfat_lookup_visit(struct inode *dp, uint inum,
                   struct exfat_file_meta *meta,
                   const char *name, void *arg)
{
  struct exfat_lookup_ctx *ctx;

  (void)dp;
  ctx = (struct exfat_lookup_ctx*)arg;
  if(!exfat_name_eq_ci(name, ctx->name))
    return 0;

  ctx->found = 1;
  ctx->inum = inum;
  memmove(&ctx->meta, meta, sizeof(*meta));
  return 1;
}

struct exfat_nth_ctx {
  uint want;
  uint idx;
  int found;
  uint inum;
  struct exfat_file_meta meta;
  char name[DIRSIZ];
};

static int
exfat_nth_visit(struct inode *dp, uint inum,
                struct exfat_file_meta *meta,
                const char *name, void *arg)
{
  struct exfat_nth_ctx *ctx;

  (void)dp;
  ctx = (struct exfat_nth_ctx*)arg;
  if(ctx->idx++ != ctx->want)
    return 0;

  ctx->found = 1;
  ctx->inum = inum;
  memmove(&ctx->meta, meta, sizeof(*meta));
  safestrcpy(ctx->name, (char*)name, sizeof(ctx->name));
  return 1;
}

static struct inode*
exfat_make_inode(uint dev, uint inum, ushort attrs,
                 uint first_cluster, uint64_t size,
                 uchar stream_flags,
                 uint parent_inum, uint parent_cluster)
{
  struct inode *ip;
  short mode;

  ip = iget(dev, inum);
  if(ip == 0)
    return 0;

  mode = (attrs & EXFAT_ATTR_RDONLY) ? 0555 : 0777;
  if(attrs & EXFAT_ATTR_DIR)
    mode |= M_IFDIR;
  else
    mode |= M_IFREG;

  acquiresleep(&ip->lock);
  ip->dev = dev;
  ip->inum = inum;
  ip->valid = 1;
  ip->type = (attrs & EXFAT_ATTR_DIR) ? T_DIR : T_FILE;
  ip->major = 0;
  ip->minor = 0;
  ip->nlink = 1;
  ip->uid = 0;
  ip->gid = 0;
  ip->mode = mode;
  ip->size = (size > 0xFFFFFFFFULL) ? 0xFFFFFFFFU : (uint)size;
  memset(ip->addrs, 0, sizeof(ip->addrs));
  ip->addrs[0] = first_cluster;
  ip->addrs[1] = stream_flags;
  ip->addrs[2] = parent_inum;
  ip->addrs[3] = parent_cluster;
  releasesleep(&ip->lock);

  return ip;
}

static struct inode*
exfat_root_inode(struct vfs *fs)
{
  struct exfat_mount_data *md;

  if(fs == 0)
    return 0;
  md = (struct exfat_mount_data*)fs->fs_data;
  if(md == 0)
    return 0;

  return exfat_make_inode(md->dev, EXFAT_INUM_ROOT,
                          EXFAT_ATTR_DIR,
                          md->root_dir_cluster, 0,
                          0, EXFAT_INUM_ROOT, md->root_dir_cluster);
}

static struct inode*
exfat_make_dotdot(struct inode *dp)
{
  uint pinum;
  uint pclu;

  if(dp == 0)
    return 0;
  if(dp->inum == EXFAT_INUM_ROOT)
    return idup(dp);

  pinum = dp->addrs[2];
  pclu = dp->addrs[3];
  if(pinum == 0)
    pinum = EXFAT_INUM_ROOT;
  if(pclu < EXFAT_CLUSTER_FIRST)
    pclu = dp->addrs[0];

  return exfat_make_inode(dp->dev, pinum, EXFAT_ATTR_DIR,
                          pclu, 0, 0,
                          EXFAT_INUM_ROOT, pclu);
}

static struct inode*
exfat_dirlookup(struct inode *dp, char *name, uint *poff)
{
  struct exfat_lookup_ctx ctx;

  if(dp == 0 || name == 0)
    return 0;
  if(dp->type != T_DIR)
    return 0;

  if(namecmp(name, ".") == 0)
    return idup(dp);
  if(namecmp(name, "..") == 0)
    return exfat_make_dotdot(dp);

  memset(&ctx, 0, sizeof(ctx));
  ctx.name = name;
  if(exfat_dir_iter(dp, exfat_lookup_visit, &ctx) < 0)
    return 0;
  if(!ctx.found)
    return 0;

  if(poff)
    *poff = ctx.inum;
  return exfat_make_inode(dp->dev, ctx.inum, ctx.meta.attrs,
                          ctx.meta.first_cluster,
                          ctx.meta.data_length,
                          ctx.meta.stream_flags,
                          dp->inum, dp->addrs[0]);
}

static struct inode*
exfat_walk(struct vfs *fs, char *path, int nameiparent, char *name)
{
  struct inode *ip;
  char elem[DIRSIZ + 1];
  char *p;

  if(fs == 0 || path == 0)
    return 0;

  if(path[0] == '/')
    ip = exfat_root_inode(fs);
  else {
    ip = proc_cwd_idup();
    if(ip == 0 || exfat_data_for_dev(ip->dev) == 0){
      if(ip)
        iput(ip);
      ip = exfat_root_inode(fs);
    }
  }
  if(ip == 0)
    return 0;

  p = path;
  while(*p){
    int advanced;
    struct inode *next;

    advanced = exfat_skipelem(p, elem);
    if(advanced == 0)
      break;
    p += advanced;

    if(elem[0] == 0)
      continue;

    if(nameiparent && *p == 0){
      int i;

      if(name){
        for(i = 0; i < DIRSIZ + 1; i++)
          name[i] = 0;
        for(i = 0; elem[i] && i < DIRSIZ; i++)
          name[i] = elem[i];
      }
      return ip;
    }

    next = exfat_dirlookup(ip, elem, 0);
    if(next == 0){
      iput(ip);
      return 0;
    }
    iput(ip);
    ip = next;
  }

  if(nameiparent){
    iput(ip);
    return 0;
  }
  return ip;
}

static struct inode*
exfat_namei(struct vfs *fs, char *path)
{
  return exfat_walk(fs, path, 0, 0);
}

static struct inode*
exfat_nameiparent(struct vfs *fs, char *path, char *name)
{
  return exfat_walk(fs, path, 1, name);
}

static void
exfat_inode_put(struct inode *ip)
{
  iput(ip);
}

static int
exfat_read(struct inode *ip, char *dst, uint off, uint n)
{
  struct exfat_mount_data *md;

  if(ip == 0 || dst == 0)
    return -1;

  md = exfat_data_for_dev(ip->dev);
  if(md == 0)
    return -1;

  if(ip->type == T_DIR){
    struct exfat_nth_ctx ctx;
    struct dirent de;
    uint want;
    uint cpy;

    if(n != sizeof(struct dirent))
      return -1;
    if((off % sizeof(struct dirent)) != 0)
      return -1;

    want = off / sizeof(struct dirent);
    memset(&ctx, 0, sizeof(ctx));
    ctx.want = want;
    if(exfat_dir_iter(ip, exfat_nth_visit, &ctx) < 0)
      return -1;
    if(!ctx.found)
      return 0;

    memset(&de, 0, sizeof(de));
    de.inum = (ushort)exfat_visible_inum(ctx.inum);
    cpy = strlen(ctx.name);
    if(cpy > DIRSIZ)
      cpy = DIRSIZ;
    memmove(de.name, ctx.name, cpy);
    memmove(dst, &de, sizeof(de));
    return sizeof(de);
  }

  if(ip->type != T_FILE)
    return -1;

  return exfat_stream_read(md, ip->addrs[0], (uchar)ip->addrs[1],
                           ip->size, dst, off, n);
}

static int
exfat_write(struct inode *ip, char *src, uint off, uint n)
{
  (void)ip;
  (void)src;
  (void)off;
  (void)n;
  return -1;
}

static int
exfat_truncate(struct inode *ip)
{
  (void)ip;
  return -1;
}

static int
exfat_stat(struct inode *ip, struct stat *st)
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
exfat_access(struct inode *ip, int mode)
{
  if(ip == 0)
    return -1;
  if((mode & IACC_READ) && (ip->mode & 0444) == 0)
    return -1;
  if((mode & IACC_WRITE) && (ip->mode & 0222) == 0)
    return -1;
  if((mode & IACC_EXEC) && (ip->mode & 0111) == 0)
    return -1;
  return 0;
}

static int
exfat_mount_init(struct mount *m)
{
  struct exfat_mount_data *md;
  struct buf *b;
  struct exfat_boot_sector *bs;
  uint nblocks;
  uint bps;
  uint spc;

  if(m == 0)
    return -1;
  nblocks = bdev_nblocks(m->dev);
  if(nblocks == 0)
    return -1;

  b = bread(m->dev, 0);
  if(b == 0)
    return -1;

  bs = (struct exfat_boot_sector*)b->data;
  if(bs->boot_signature != EXFAT_BOOT_SIG){
    brelse(b);
    return -1;
  }
  if(memcmp(bs->fs_name, "EXFAT   ", 8) != 0){
    brelse(b);
    return -1;
  }

  bps = 1U << bs->bytes_per_sector_shift;
  spc = 1U << bs->sectors_per_cluster_shift;
  if(bps != BSIZE || spc == 0){
    brelse(b);
    return -1;
  }
  if(bs->num_fats < 1 || bs->cluster_count == 0 || bs->root_dir_cluster < EXFAT_CLUSTER_FIRST){
    brelse(b);
    return -1;
  }
  if(bs->fat_length == 0 || bs->fat_offset == 0 || bs->cluster_heap_offset == 0){
    brelse(b);
    return -1;
  }
  if(bs->cluster_heap_offset >= nblocks || bs->fat_offset >= nblocks){
    brelse(b);
    return -1;
  }

  md = (struct exfat_mount_data*)kalloc();
  if(md == 0){
    brelse(b);
    return -1;
  }
  memset(md, 0, sizeof(*md));

  md->dev = m->dev;
  md->fat_offset = bs->fat_offset;
  md->fat_length = bs->fat_length;
  md->cluster_heap_offset = bs->cluster_heap_offset;
  md->cluster_count = bs->cluster_count;
  md->root_dir_cluster = bs->root_dir_cluster;
  md->sectors_per_cluster = spc;
  md->bytes_per_cluster = spc * BSIZE;
  md->volume_flags = bs->volume_flags;
  md->num_fats = bs->num_fats;

  brelse(b);

  m->fs_data = (void*)md;
  cprintf("exfat: mounted dev=%d clusters=%d spc=%d\n",
          m->dev, md->cluster_count, md->sectors_per_cluster);
  return 0;
}

static void
exfat_fs_destroy(struct vfs *fs)
{
  if(fs == 0 || fs->fs_data == 0)
    return;
  kfree((char*)fs->fs_data);
  fs->fs_data = 0;
}

static struct exfat_mount_data*
exfat_data_for_dev(uint dev)
{
  return (struct exfat_mount_data*)vfs_dev_fs_data(dev);
}

static struct vfs_ops exfat_vfs_ops = {
  .root_inode = exfat_root_inode,
  .namei = exfat_namei,
  .nameiparent = exfat_nameiparent,
  .inode_put = exfat_inode_put,
};

static struct vnode_ops exfat_vnode_ops = {
  .read = exfat_read,
  .write = exfat_write,
  .truncate = exfat_truncate,
  .drop = 0,
  .stat = exfat_stat,
  .setattr = 0,
  .access = exfat_access,
  .dirlookup = exfat_dirlookup,
  .dirlink = 0,
  .link = 0,
  .remove = 0,
  .rename = 0,
  .faultctl = 0,
  .create = 0,
  .readlink = 0,
  .symlink = 0,
};

void
vfs_exfat_init(struct vfs *fs)
{
  if(fs == 0)
    return;

  safestrcpy(fs->name, "exfat", VFS_NAME_MAX);
  fs->caps = VFS_CAP_READ;
  fs->fs_data = 0;
  fs->fs_destroy = exfat_fs_destroy;
  fs->mount_init = exfat_mount_init;
  fs->ops = exfat_vfs_ops;
  fs->vnode_ops = exfat_vnode_ops;
}
