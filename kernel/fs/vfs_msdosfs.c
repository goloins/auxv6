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

// Minimal msdosfs (FAT16/32) backend scaffold.
// This is intentionally non-root and read-only for now.

#define MSDOS_BOOT_SIG_OFF 510
#define MSDOS_BOOT_SIG 0xAA55
#define MSDOS_ATTR_RDONLY 0x01
#define MSDOS_ATTR_DIR 0x10
#define MSDOS_ATTR_VOLID 0x08
#define MSDOS_ATTR_LFN 0x0F
#define MSDOS_EOC16 0xFFF8
#define MSDOS_EOC32 0x0FFFFFF8

struct fat_dirent {
  uchar name[11];
  uchar attr;
  uchar ntres;
  uchar crt_tenth;
  ushort crt_time;
  ushort crt_date;
  ushort acc_date;
  ushort clu_hi;
  ushort wrt_time;
  ushort wrt_date;
  ushort clu_lo;
  uint size;
} __attribute__((packed));

struct fat_bpb_common {
  uchar jump[3];
  uchar oem[8];
  ushort byts_per_sec;
  uchar sec_per_clus;
  ushort rsvd_sec_cnt;
  uchar num_fats;
  ushort root_ent_cnt;
  ushort tot_sec16;
  uchar media;
  ushort fatsz16;
  ushort sec_per_trk;
  ushort num_heads;
  uint hidd_sec;
  uint tot_sec32;
} __attribute__((packed));

struct fat_bpb_fat32 {
  uint fatsz32;
  ushort ext_flags;
  ushort fs_ver;
  uint root_clus;
  ushort fs_info;
  ushort bk_boot_sec;
  uchar reserved[12];
} __attribute__((packed));

struct msdos_mount_data {
  int dev;
  int fat_type;     // 16 or 32
  uint bytes_per_sector;
  uint sectors_per_cluster;
  uint reserved_sectors;
  uint num_fats;
  uint sectors_per_fat;
  uint root_dir_entries;
  uint root_cluster;
  uint root_start;
  uint root_sectors;
  uint fat_start;
  uint data_start;
};

static uint msdos_active_dev;
static struct msdos_mount_data *msdos_bootstrap_data;

static struct inode* msdos_root_inode(void);
static struct inode* msdos_dirlookup(struct inode *dp, char *name, uint *poff);

static struct msdos_mount_data*
msdos_data_for_dev(uint dev)
{
  struct msdos_mount_data *md;

  md = (struct msdos_mount_data*)vfs_dev_fs_data(dev);
  if(md == 0 && msdos_bootstrap_data && (uint)msdos_bootstrap_data->dev == dev)
    md = msdos_bootstrap_data;
  return md;
}

static uint
msdos_cluster_first_sector(struct msdos_mount_data *md, uint cluster)
{
  if(md == 0 || cluster < 2)
    return 0;
  return md->data_start + (cluster - 2) * md->sectors_per_cluster;
}

static int
msdos_is_eoc(struct msdos_mount_data *md, uint cluster)
{
  if(md->fat_type == 16)
    return cluster >= MSDOS_EOC16;
  return cluster >= MSDOS_EOC32;
}

static int
msdos_next_cluster(struct msdos_mount_data *md, uint cluster, uint *next)
{
  uint off;
  uint sec;
  uint sec_off;
  struct buf *b;
  uint val;

  if(md == 0 || next == 0 || cluster < 2)
    return -1;

  if(md->fat_type == 16){
    off = cluster * 2;
  } else {
    off = cluster * 4;
  }

  sec = md->fat_start + (off / BSIZE);
  sec_off = off % BSIZE;
  b = bread(md->dev, sec);
  if(b == 0)
    return -1;

  if(md->fat_type == 16){
    if(sec_off + 1 >= BSIZE){
      brelse(b);
      return -1;
    }
    val = (uint)b->data[sec_off] | ((uint)b->data[sec_off + 1] << 8);
  } else {
    if(sec_off + 3 >= BSIZE){
      brelse(b);
      return -1;
    }
    val = (uint)b->data[sec_off] |
          ((uint)b->data[sec_off + 1] << 8) |
          ((uint)b->data[sec_off + 2] << 16) |
          ((uint)b->data[sec_off + 3] << 24);
    val &= 0x0FFFFFFF;
  }
  brelse(b);

  if(msdos_is_eoc(md, val)){
    *next = 0;
    return 0;
  }
  *next = val;
  return 0;
}

static uint
msdos_entry_cluster(struct fat_dirent *de)
{
  return (((uint)de->clu_hi) << 16) | de->clu_lo;
}

static int
msdos_entry_valid(struct fat_dirent *de, int *end)
{
  if(end)
    *end = 0;

  if(de->name[0] == 0x00){
    if(end)
      *end = 1;
    return 0;
  }
  if(de->name[0] == 0xE5)
    return 0;
  if(de->attr == MSDOS_ATTR_LFN)
    return 0;
  if(de->attr & MSDOS_ATTR_VOLID)
    return 0;
  return 1;
}

static void
msdos_entry_name(struct fat_dirent *de, char *out, uint outsz)
{
  int i;
  int j;
  int base_end;
  int ext_end;

  if(outsz == 0)
    return;
  out[0] = 0;

  base_end = 8;
  while(base_end > 0 && de->name[base_end - 1] == ' ')
    base_end--;
  ext_end = 11;
  while(ext_end > 8 && de->name[ext_end - 1] == ' ')
    ext_end--;

  j = 0;
  for(i = 0; i < base_end && (uint)(j + 1) < outsz; i++){
    char c = (char)de->name[i];
    out[j++] = c;
  }
  if(ext_end > 8 && (uint)(j + 1) < outsz)
    out[j++] = '.';
  for(i = 8; i < ext_end && (uint)(j + 1) < outsz; i++){
    char c = (char)de->name[i];
    out[j++] = c;
  }
  out[j] = 0;
}

static int
msdos_component_to_83(char *name, uchar out[11])
{
  int i;
  int j;
  int k;

  if(name == 0 || name[0] == 0)
    return -1;

  for(i = 0; i < 11; i++)
    out[i] = ' ';

  i = 0;
  j = 0;
  while(name[i] && name[i] != '.'){
    char c = name[i];
    if(c == '/' || j >= 8)
      return -1;
    if(c >= 'a' && c <= 'z')
      c -= ('a' - 'A');
    out[j++] = (uchar)c;
    i++;
  }

  if(j == 0)
    return -1;

  if(name[i] == '.')
    i++;

  k = 8;
  while(name[i]){
    char c = name[i];
    if(c == '/' || c == '.' || k >= 11)
      return -1;
    if(c >= 'a' && c <= 'z')
      c -= ('a' - 'A');
    out[k++] = (uchar)c;
    i++;
  }

  return 0;
}

static int
msdos_name_matches_83(char *name, struct fat_dirent *de)
{
  uchar want[11];

  if(msdos_component_to_83(name, want) < 0)
    return 0;
  return memcmp(want, de->name, 11) == 0;
}

static uint
msdos_min_u32(uint a, uint b)
{
  return (a < b) ? a : b;
}

static int
msdos_read_file_data(struct inode *ip, char *dst, uint off, uint n)
{
  struct msdos_mount_data *md;
  uint cluster;
  uint cluster_bytes;
  uint skip;
  uint within;
  uint done;

  if(ip == 0 || dst == 0)
    return -1;

  md = msdos_data_for_dev(ip->dev);
  if(md == 0)
    return -1;
  if(md->sectors_per_cluster == 0)
    return -1;

  if(off >= ip->size)
    return 0;
  if(off + n < off)
    return -1;
  if(off + n > ip->size)
    n = ip->size - off;

  cluster = ip->addrs[0];
  if(cluster < 2)
    return (n == 0) ? 0 : -1;

  cluster_bytes = md->sectors_per_cluster * BSIZE;
  if(cluster_bytes == 0)
    return -1;

  skip = off / cluster_bytes;
  within = off % cluster_bytes;

  while(skip > 0){
    if(msdos_next_cluster(md, cluster, &cluster) < 0)
      return -1;
    if(cluster == 0)
      return 0;
    skip--;
  }

  done = 0;
  while(done < n && cluster >= 2){
    uint csec;
    uint need;
    uint copied;

    csec = msdos_cluster_first_sector(md, cluster);
    if(csec == 0)
      return (done == 0) ? -1 : (int)done;

    need = msdos_min_u32(n - done, cluster_bytes - within);
    copied = 0;
    while(copied < need){
      uint abs_off;
      uint sec_idx;
      uint sec_off;
      uint chunk;
      struct buf *b;

      abs_off = within + copied;
      sec_idx = abs_off / BSIZE;
      sec_off = abs_off % BSIZE;
      chunk = msdos_min_u32(need - copied, BSIZE - sec_off);

      b = bread(md->dev, csec + sec_idx);
      if(b == 0)
        return (done == 0 && copied == 0) ? -1 : (int)(done + copied);
      memmove(dst + done + copied, b->data + sec_off, chunk);
      brelse(b);
      copied += chunk;
    }

    done += need;
    within = 0;

    if(done == n)
      break;
    if(msdos_next_cluster(md, cluster, &cluster) < 0)
      return (done == 0) ? -1 : (int)done;
    if(cluster == 0)
      break;
  }

  return done;
}

static struct inode*
msdos_make_inode(uint dev, uint inum, struct fat_dirent *de, int is_root16)
{
  struct inode *ip;
  uint start;

  ip = iget(dev, inum);
  if(ip == 0)
    return 0;

  ilock(ip);
  if(is_root16){
    ip->type = T_DIR;
    ip->mode = M_IFDIR | 0555;
    ip->size = 0;
    ip->addrs[0] = 0;
    ip->addrs[1] = MSDOS_ATTR_DIR;
    ip->addrs[2] = 1;
  } else {
    start = msdos_entry_cluster(de);
    if(de->attr & MSDOS_ATTR_DIR){
      ip->type = T_DIR;
      ip->mode = M_IFDIR | 0555;
      ip->size = 0;
    } else {
      ip->type = T_FILE;
      if(de->attr & MSDOS_ATTR_RDONLY)
        ip->mode = M_IFREG | 0444;
      else
        ip->mode = M_IFREG | 0644;
      ip->size = de->size;
    }
    ip->addrs[0] = start;
    ip->addrs[1] = de->attr;
    ip->addrs[2] = 0;
  }
  ip->major = 0;
  ip->minor = 0;
  ip->nlink = 1;
  ip->uid = 0;
  ip->gid = 0;
  ip->valid = 1;
  iunlock(ip);
  return ip;
}

static int
msdos_dir_scan_fat16_root(struct msdos_mount_data *md,
                          int (*visit)(struct fat_dirent*, uint, uint, void*),
                          void *arg)
{
  uint slot;

  for(slot = 0; slot < md->root_dir_entries; slot++){
    uint byte_off;
    uint sec;
    uint sec_off;
    struct buf *b;
    struct fat_dirent de;
    int end;

    byte_off = slot * sizeof(struct fat_dirent);
    sec = md->root_start + (byte_off / BSIZE);
    sec_off = byte_off % BSIZE;

    b = bread(md->dev, sec);
    if(b == 0)
      return -1;
    memmove(&de, b->data + sec_off, sizeof(de));
    brelse(b);

    if(!msdos_entry_valid(&de, &end)){
      if(end)
        return 0;
      continue;
    }

    if(visit(&de, 0x10000000U + slot, slot, arg) != 0)
      return 1;
  }

  return 0;
}

static int
msdos_dir_scan_cluster_chain(struct msdos_mount_data *md, uint first_cluster,
                             int (*visit)(struct fat_dirent*, uint, uint, void*),
                             void *arg)
{
  uint cluster;
  uint vis;

  if(first_cluster < 2)
    return 0;

  cluster = first_cluster;
  vis = 0;
  while(cluster >= 2){
    uint csec;
    uint s;

    csec = msdos_cluster_first_sector(md, cluster);
    if(csec == 0)
      return -1;

    for(s = 0; s < md->sectors_per_cluster; s++){
      struct buf *b;
      uint off;

      b = bread(md->dev, csec + s);
      if(b == 0)
        return -1;

      for(off = 0; off + sizeof(struct fat_dirent) <= BSIZE; off += sizeof(struct fat_dirent)){
        struct fat_dirent de;
        uint slot_in_cluster;
        uint inum;
        int end;

        memmove(&de, b->data + off, sizeof(de));
        if(!msdos_entry_valid(&de, &end)){
          if(end){
            brelse(b);
            return 0;
          }
          continue;
        }

        slot_in_cluster = (s * BSIZE + off) / sizeof(struct fat_dirent);
        inum = ((cluster & 0xFFFFU) << 16) | (slot_in_cluster & 0xFFFFU);
        if(inum == ROOTINO)
          inum++;

        if(visit(&de, inum, vis, arg) != 0){
          brelse(b);
          return 1;
        }
        vis++;
      }

      brelse(b);
    }

    if(msdos_next_cluster(md, cluster, &cluster) < 0)
      return -1;
    if(cluster == 0)
      return 0;
  }

  return 0;
}

static int
msdos_dir_scan(struct inode *dp,
               int (*visit)(struct fat_dirent*, uint, uint, void*),
               void *arg)
{
  struct msdos_mount_data *md;

  if(dp == 0 || visit == 0)
    return -1;
  if(dp->type != T_DIR)
    return -1;

  md = msdos_data_for_dev(dp->dev);
  if(md == 0)
    return -1;

  if(md->fat_type == 16 && dp->inum == ROOTINO && dp->addrs[2] == 1)
    return msdos_dir_scan_fat16_root(md, visit, arg);

  return msdos_dir_scan_cluster_chain(md, dp->addrs[0], visit, arg);
}

struct lookup_ctx {
  char *name;
  struct fat_dirent de;
  uint inum;
  int found;
};

static int
msdos_lookup_visit(struct fat_dirent *de, uint inum, uint visidx, void *arg)
{
  struct lookup_ctx *ctx;
  (void)visidx;

  ctx = (struct lookup_ctx*)arg;
  if(msdos_name_matches_83(ctx->name, de)){
    memmove(&ctx->de, de, sizeof(*de));
    ctx->inum = inum;
    ctx->found = 1;
    return 1;
  }
  return 0;
}

struct nth_ctx {
  uint want;
  uint cur;
  struct fat_dirent de;
  uint inum;
  int found;
};

static int
msdos_nth_visit(struct fat_dirent *de, uint inum, uint visidx, void *arg)
{
  struct nth_ctx *ctx;
  (void)visidx;

  ctx = (struct nth_ctx*)arg;
  if(ctx->cur == ctx->want){
    memmove(&ctx->de, de, sizeof(*de));
    ctx->inum = inum;
    ctx->found = 1;
    return 1;
  }
  ctx->cur++;
  return 0;
}

static struct inode*
msdos_walk(char *path, int nameiparent, char *name)
{
  struct inode *ip;
  char elem[DIRSIZ + 1];
  int i;
  int e;

  if(path == 0)
    return 0;

  ip = msdos_root_inode();
  if(ip == 0)
    return 0;

  i = 0;
  while(path[i] == '/')
    i++;

  for(;;){
    int len;
    struct inode *next;

    if(path[i] == 0)
      break;

    len = 0;
    while(path[i] && path[i] != '/'){
      if(len < DIRSIZ)
        elem[len++] = path[i];
      i++;
    }
    elem[len] = 0;
    while(path[i] == '/')
      i++;

    if(elem[0] == 0)
      continue;

    if(nameiparent && path[i] == 0){
      for(e = 0; e < DIRSIZ; e++)
        name[e] = 0;
      for(e = 0; elem[e] && e < DIRSIZ; e++)
        name[e] = elem[e];
      return ip;
    }

    if(namecmp(elem, ".") == 0)
      continue;

    next = msdos_dirlookup(ip, elem, 0);
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

static int
msdos_valid_signature(struct buf *b)
{
  ushort sig;

  sig = (ushort)b->data[MSDOS_BOOT_SIG_OFF] |
        ((ushort)b->data[MSDOS_BOOT_SIG_OFF + 1] << 8);
  return sig == MSDOS_BOOT_SIG;
}

static int
msdos_mount_init(struct mount *m)
{
  struct msdos_mount_data *md;
  struct buf *b;
  struct fat_bpb_common *bpb;
  struct fat_bpb_fat32 *bpb32;
  uint fatsz;
  uint totsec;
  uint rootsz;
  uint datasz;
  uint clusters;

  if(m == 0)
    return -1;
  if(bdev_nblocks(m->dev) == 0)
    return -1;

  b = bread(m->dev, 0);
  if(b == 0)
    return -1;

  if(!msdos_valid_signature(b)){
    brelse(b);
    return -1;
  }

  bpb = (struct fat_bpb_common*)b->data;
  if(bpb->byts_per_sec == 0 || bpb->sec_per_clus == 0 || bpb->num_fats == 0){
    brelse(b);
    return -1;
  }

  md = (struct msdos_mount_data*)kalloc();
  if(md == 0){
    brelse(b);
    return -1;
  }
  memset(md, 0, sizeof(*md));

  md->dev = m->dev;
  md->bytes_per_sector = bpb->byts_per_sec;
  md->sectors_per_cluster = bpb->sec_per_clus;
  md->reserved_sectors = bpb->rsvd_sec_cnt;
  md->num_fats = bpb->num_fats;
  md->root_dir_entries = bpb->root_ent_cnt;

  bpb32 = (struct fat_bpb_fat32*)(b->data + 36);
  fatsz = bpb->fatsz16 ? bpb->fatsz16 : bpb32->fatsz32;
  totsec = bpb->tot_sec16 ? bpb->tot_sec16 : bpb->tot_sec32;
  rootsz = ((md->root_dir_entries * 32) + (md->bytes_per_sector - 1)) / md->bytes_per_sector;

  if(fatsz == 0 || totsec == 0){
    brelse(b);
    kfree((char*)md);
    return -1;
  }

  md->sectors_per_fat = fatsz;
  md->fat_start = md->reserved_sectors;
  md->root_start = md->reserved_sectors + (md->num_fats * fatsz);
  md->root_sectors = rootsz;
  md->data_start = md->root_start + rootsz;

  if(md->bytes_per_sector != BSIZE){
    brelse(b);
    kfree((char*)md);
    return -1;
  }

  if(totsec <= md->data_start){
    brelse(b);
    kfree((char*)md);
    return -1;
  }

  datasz = totsec - md->data_start;
  clusters = datasz / md->sectors_per_cluster;

  if(clusters < 65525){
    md->fat_type = 16;
    md->root_cluster = 0;
  } else {
    md->fat_type = 32;
    md->root_cluster = bpb32->root_clus;
  }

  brelse(b);

  m->fs_data = (void*)md;
  msdos_active_dev = (uint)m->dev;
  if(m->path[0] == '/' && m->path[1] == 0)
    msdos_bootstrap_data = md;

  cprintf("msdosfs: mounted dev=%d FAT%d\n", m->dev, md->fat_type);
  return 0;
}

static struct inode*
msdos_root_inode(void)
{
  struct msdos_mount_data *md;
  struct fat_dirent rootde;

  md = msdos_data_for_dev(msdos_active_dev);
  if(md == 0)
    return 0;

  memset(&rootde, 0, sizeof(rootde));
  rootde.attr = MSDOS_ATTR_DIR;
  if(md->fat_type == 32){
    rootde.clu_lo = (ushort)(md->root_cluster & 0xFFFF);
    rootde.clu_hi = (ushort)((md->root_cluster >> 16) & 0xFFFF);
    return msdos_make_inode(msdos_active_dev, ROOTINO, &rootde, 0);
  }

  return msdos_make_inode(msdos_active_dev, ROOTINO, &rootde, 1);
}

static struct inode*
msdos_namei(char *path)
{
  return msdos_walk(path, 0, 0);
}

static struct inode*
msdos_nameiparent(char *path, char *name)
{
  return msdos_walk(path, 1, name);
}

static void
msdos_inode_put(struct inode *ip)
{
  iput(ip);
}

static int
msdos_read(struct inode *ip, char *dst, uint off, uint n)
{
  struct nth_ctx ctx;
  struct dirent de;
  char nm[16];
  uint cpy;

  if(ip == 0 || dst == 0)
    return -1;

  if(ip->type == T_DIR){
    if(n != sizeof(struct dirent))
      return -1;
    if((off % sizeof(struct dirent)) != 0)
      return -1;

    memset(&ctx, 0, sizeof(ctx));
    ctx.want = off / sizeof(struct dirent);
    if(msdos_dir_scan(ip, msdos_nth_visit, &ctx) < 0)
      return -1;
    if(!ctx.found)
      return 0;

    memset(&de, 0, sizeof(de));
    de.inum = (ushort)(ctx.inum & 0xFFFF);
    memset(nm, 0, sizeof(nm));
    msdos_entry_name(&ctx.de, nm, sizeof(nm));
    cpy = strlen(nm);
    if(cpy > DIRSIZ)
      cpy = DIRSIZ;
    memmove(de.name, nm, cpy);
    memmove(dst, &de, sizeof(de));
    return sizeof(de);
  }

  if(ip->type != T_FILE)
    return -1;

  return msdos_read_file_data(ip, dst, off, n);
}

static int
msdos_write_file_data(struct inode *ip, char *src, uint off, uint n)
{
  struct msdos_mount_data *md;
  uint cluster;
  uint cluster_bytes;
  uint skip;
  uint within;
  uint done;

  if(ip == 0 || src == 0)
    return -1;

  md = msdos_data_for_dev(ip->dev);
  if(md == 0)
    return -1;
  if(md->sectors_per_cluster == 0)
    return -1;

  if(n == 0)
    return 0;
  // Staged write support: in-place only, no sparse write and no growth.
  if(off > ip->size)
    return -1;
  if(off + n < off)
    return -1;
  if(off + n > ip->size)
    return -1;

  cluster = ip->addrs[0];
  if(cluster < 2)
    return -1;

  cluster_bytes = md->sectors_per_cluster * BSIZE;
  if(cluster_bytes == 0)
    return -1;

  skip = off / cluster_bytes;
  within = off % cluster_bytes;

  while(skip > 0){
    if(msdos_next_cluster(md, cluster, &cluster) < 0)
      return -1;
    if(cluster == 0)
      return -1;
    skip--;
  }

  done = 0;
  while(done < n && cluster >= 2){
    uint csec;
    uint need;
    uint copied;

    csec = msdos_cluster_first_sector(md, cluster);
    if(csec == 0)
      return (done == 0) ? -1 : (int)done;

    need = msdos_min_u32(n - done, cluster_bytes - within);
    copied = 0;
    while(copied < need){
      uint abs_off;
      uint sec_idx;
      uint sec_off;
      uint chunk;
      struct buf *b;

      abs_off = within + copied;
      sec_idx = abs_off / BSIZE;
      sec_off = abs_off % BSIZE;
      chunk = msdos_min_u32(need - copied, BSIZE - sec_off);

      b = bread(md->dev, csec + sec_idx);
      if(b == 0)
        return (done == 0 && copied == 0) ? -1 : (int)(done + copied);
      memmove(b->data + sec_off, src + done + copied, chunk);
      bwrite(b);
      brelse(b);
      copied += chunk;
    }

    done += need;
    within = 0;

    if(done == n)
      break;
    if(msdos_next_cluster(md, cluster, &cluster) < 0)
      return (done == 0) ? -1 : (int)done;
    if(cluster == 0)
      return (done == 0) ? -1 : (int)done;
  }

  return done;
}

static int
msdos_write(struct inode *ip, char *src, uint off, uint n)
{
  if(ip == 0 || src == 0)
    return -1;
  if(ip->type != T_FILE)
    return -1;
  if((ip->mode & 0222) == 0)
    return -1;

  return msdos_write_file_data(ip, src, off, n);
}

static int
msdos_truncate(struct inode *ip)
{
  (void)ip;
  return -1;
}

static int
msdos_stat(struct inode *ip, struct stat *st)
{
  if(ip == 0 || st == 0)
    return -1;
  st->type = ip->type;
  st->dev = ip->dev;
  st->ino = ip->inum;
  st->major = ip->major;
  st->minor = ip->minor;
  st->nlink = ip->nlink;
  st->uid = ip->uid;
  st->gid = ip->gid;
  st->mode = ip->mode;
  st->size = ip->size;
  return 0;
}

static int
msdos_access(struct inode *ip, int mode)
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

static struct inode*
msdos_dirlookup(struct inode *dp, char *name, uint *poff)
{
  struct lookup_ctx ctx;

  if(dp == 0 || name == 0)
    return 0;
  if(dp->type != T_DIR)
    return 0;

  if(namecmp(name, ".") == 0)
    return idup(dp);
  if(namecmp(name, "..") == 0 && dp->inum == ROOTINO)
    return idup(dp);

  memset(&ctx, 0, sizeof(ctx));
  ctx.name = name;
  if(msdos_dir_scan(dp, msdos_lookup_visit, &ctx) < 0)
    return 0;
  if(!ctx.found)
    return 0;

  if(poff)
    *poff = ctx.inum;
  return msdos_make_inode(dp->dev, ctx.inum, &ctx.de, 0);
}

void
vfs_msdosfs_init(struct vfs *fs)
{
  if(fs == 0)
    return;

  safestrcpy(fs->name, "msdosfs", sizeof(fs->name));
  fs->caps = VFS_CAP_READ | VFS_CAP_WRITE;
  fs->fs_data = 0;
  fs->fs_destroy = 0;
  fs->mount_init = msdos_mount_init;
  fs->ops.root_inode = msdos_root_inode;
  fs->ops.namei = msdos_namei;
  fs->ops.nameiparent = msdos_nameiparent;
  fs->ops.inode_put = msdos_inode_put;

  fs->vnode_ops.read = msdos_read;
  fs->vnode_ops.write = msdos_write;
  fs->vnode_ops.truncate = msdos_truncate;
  fs->vnode_ops.stat = msdos_stat;
  fs->vnode_ops.access = msdos_access;
  fs->vnode_ops.dirlookup = msdos_dirlookup;
}
