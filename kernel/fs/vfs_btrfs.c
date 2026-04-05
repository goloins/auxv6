#include "types.h"
#include "defs.h"
#include "param.h"
#include "stat.h"
#include "spinlock.h"
#include "sleeplock.h"
#include "mmu.h"
#include "fs.h"
#include "file.h"
#include "buf.h"
#include "vfs.h"
#include "blockdev.h"
#include "stddef.h"
#include "stdint.h"

#define BTRFS_SUPER_INFO_OFFSET 65536ULL
#define BTRFS_SUPER_INFO_SIZE   4096
#define BTRFS_MAGIC             0x4D5F53665248425FULL

#define BTRFS_ROOT_TREE_OBJECTID      1ULL
#define BTRFS_CHUNK_TREE_OBJECTID     3ULL
#define BTRFS_FS_TREE_OBJECTID        5ULL
#define BTRFS_ROOT_TREE_DIR_OBJECTID  6ULL
#define BTRFS_FIRST_FREE_OBJECTID     256ULL
#define BTRFS_FIRST_CHUNK_TREE_OBJECTID 256ULL

#define BTRFS_INODE_ITEM_KEY    1
#define BTRFS_INODE_REF_KEY     12
#define BTRFS_INODE_EXTREF_KEY  13
#define BTRFS_DIR_ITEM_KEY      84
#define BTRFS_DIR_INDEX_KEY     96
#define BTRFS_EXTENT_DATA_KEY   108
#define BTRFS_ROOT_ITEM_KEY     132
#define BTRFS_ROOT_BACKREF_KEY  144
#define BTRFS_ROOT_REF_KEY      156
#define BTRFS_CHUNK_ITEM_KEY    228

#define BTRFS_FT_REG_FILE 1
#define BTRFS_FT_DIR      2
#define BTRFS_FT_SYMLINK  7

#define BTRFS_FILE_EXTENT_INLINE   0
#define BTRFS_FILE_EXTENT_REG      1
#define BTRFS_FILE_EXTENT_PREALLOC 2

#define BTRFS_MAX_CHUNK_MAPS 128

#define BTRFS_IP_TREE_BYTENR_LO 0
#define BTRFS_IP_TREE_BYTENR_HI 1
#define BTRFS_IP_TREE_LEVEL     2
#define BTRFS_IP_OBJECTID_HI    3
#define BTRFS_IP_ROOTDIR_LO     4
#define BTRFS_IP_ROOTDIR_HI     5
#define BTRFS_IP_TREEID_LO      6
#define BTRFS_IP_TREEID_HI      7

struct btrfs_disk_key {
  uint64_t objectid;
  uchar type;
  uint64_t offset;
} __attribute__((packed));

struct btrfs_key {
  uint64_t objectid;
  uchar type;
  uint64_t offset;
};

struct btrfs_header {
  uchar csum[32];
  uchar fsid[16];
  uint64_t bytenr;
  uint64_t flags;
  uchar chunk_tree_uuid[16];
  uint64_t generation;
  uint64_t owner;
  uint32_t nritems;
  uchar level;
} __attribute__((packed));

struct btrfs_item {
  struct btrfs_disk_key key;
  uint32_t offset;
  uint32_t size;
} __attribute__((packed));

struct btrfs_key_ptr {
  struct btrfs_disk_key key;
  uint64_t blockptr;
  uint64_t generation;
} __attribute__((packed));

struct btrfs_dev_item {
  uint64_t devid;
  uint64_t total_bytes;
  uint64_t bytes_used;
  uint32_t io_align;
  uint32_t io_width;
  uint32_t sector_size;
  uint64_t type;
  uint64_t generation;
  uint64_t start_offset;
  uint32_t dev_group;
  uchar seek_speed;
  uchar bandwidth;
  uchar uuid[16];
  uchar fsid[16];
} __attribute__((packed));

struct btrfs_stripe {
  uint64_t devid;
  uint64_t offset;
  uchar dev_uuid[16];
} __attribute__((packed));

struct btrfs_chunk {
  uint64_t length;
  uint64_t owner;
  uint64_t stripe_len;
  uint64_t type;
  uint32_t io_align;
  uint32_t io_width;
  uint32_t sector_size;
  uint16_t num_stripes;
  uint16_t sub_stripes;
  struct btrfs_stripe stripe;
} __attribute__((packed));

struct btrfs_timespec {
  uint64_t sec;
  uint32_t nsec;
} __attribute__((packed));

struct btrfs_inode_item {
  uint64_t generation;
  uint64_t transid;
  uint64_t size;
  uint64_t nbytes;
  uint64_t block_group;
  uint32_t nlink;
  uint32_t uid;
  uint32_t gid;
  uint32_t mode;
  uint64_t rdev;
  uint64_t flags;
  uint64_t sequence;
  uint64_t reserved[4];
  struct btrfs_timespec atime;
  struct btrfs_timespec ctime;
  struct btrfs_timespec mtime;
  struct btrfs_timespec otime;
} __attribute__((packed));

struct btrfs_inode_extref {
  uint64_t parent_objectid;
  uint64_t index;
  uint16_t name_len;
} __attribute__((packed));

struct btrfs_dir_item {
  struct btrfs_disk_key location;
  uint64_t transid;
  uint16_t data_len;
  uint16_t name_len;
  uchar type;
} __attribute__((packed));

struct btrfs_root_item {
  struct btrfs_inode_item inode;
  uint64_t generation;
  uint64_t root_dirid;
  uint64_t bytenr;
  uint64_t byte_limit;
  uint64_t bytes_used;
  uint64_t last_snapshot;
  uint64_t flags;
  uint32_t refs;
  struct btrfs_disk_key drop_progress;
  uchar drop_level;
  uchar level;
  uint64_t generation_v2;
  uchar uuid[16];
  uchar parent_uuid[16];
  uchar received_uuid[16];
  uint64_t ctransid;
  uint64_t otransid;
  uint64_t stransid;
  uint64_t rtransid;
  struct btrfs_timespec ctime;
  struct btrfs_timespec otime;
  struct btrfs_timespec stime;
  struct btrfs_timespec rtime;
  uint64_t reserved[8];
} __attribute__((packed));

struct btrfs_file_extent_item {
  uint64_t generation;
  uint64_t ram_bytes;
  uchar compression;
  uchar encryption;
  uint16_t other_encoding;
  uchar type;
  uint64_t disk_bytenr;
  uint64_t disk_num_bytes;
  uint64_t offset;
  uint64_t num_bytes;
} __attribute__((packed));

struct btrfs_super_block {
  uchar csum[32];
  uchar fsid[16];
  uint64_t bytenr;
  uint64_t flags;
  uint64_t magic;
  uint64_t generation;
  uint64_t root;
  uint64_t chunk_root;
  uint64_t log_root;
  uint64_t unused_log_root_transid;
  uint64_t total_bytes;
  uint64_t bytes_used;
  uint64_t root_dir_objectid;
  uint64_t num_devices;
  uint32_t sectorsize;
  uint32_t nodesize;
  uint32_t unused_leafsize;
  uint32_t stripesize;
  uint32_t sys_chunk_array_size;
  uint64_t chunk_root_generation;
  uint64_t compat_flags;
  uint64_t compat_ro_flags;
  uint64_t incompat_flags;
  uint16_t csum_type;
  uchar root_level;
  uchar chunk_root_level;
  uchar log_root_level;
  struct btrfs_dev_item dev_item;
  char label[256];
  uint64_t cache_generation;
  uint64_t uuid_tree_generation;
  uchar reserved0[0xf0];
  uchar sys_chunk_array[2048];
} __attribute__((packed));

struct btrfs_chunk_map {
  uint64_t logical;
  uint64_t length;
  uint64_t physical;
  uint64_t devid;
  uint64_t flags;
  ushort stripes;
  uchar valid;
};

struct btrfs_mount_data {
  int dev;
  struct spinlock lock;
  struct btrfs_super_block sb;
  struct btrfs_chunk_map maps[BTRFS_MAX_CHUNK_MAPS];
  int map_count;
  uint64_t mounted_tree_id;
  uint64_t mounted_tree_bytenr;
  uint64_t mounted_root_dirid;
  uchar mounted_tree_level;
};

typedef int (*btrfs_item_cb)(struct btrfs_mount_data *md, void *leaf,
                             struct btrfs_key *key, void *item_ptr,
                             uint32_t item_size, void *arg);

static struct btrfs_mount_data* btrfs_data_for_dev(uint dev);

static uint16_t
btrfs_get_u16(const void *p)
{
  uint16_t v;

  memmove(&v, p, sizeof(v));
  return v;
}

static uint32_t
btrfs_get_u32(const void *p)
{
  uint32_t v;

  memmove(&v, p, sizeof(v));
  return v;
}

static uint64_t
btrfs_get_u64(const void *p)
{
  uint64_t v;

  memmove(&v, p, sizeof(v));
  return v;
}

static uint64_t
btrfs_chunk_item_size(uint16_t stripes)
{
  return offsetof(struct btrfs_chunk, stripe) +
         (uint64_t)stripes * sizeof(struct btrfs_stripe);
}

static struct btrfs_key
btrfs_disk_key_to_cpu(struct btrfs_disk_key *dk)
{
  struct btrfs_key key;

  key.objectid = btrfs_get_u64(&dk->objectid);
  key.type = dk->type;
  key.offset = btrfs_get_u64(&dk->offset);
  return key;
}

static uint64_t
btrfs_ip_tree_bytenr(struct inode *ip)
{
  return ((uint64_t)ip->addrs[BTRFS_IP_TREE_BYTENR_HI] << 32) |
         (uint64_t)ip->addrs[BTRFS_IP_TREE_BYTENR_LO];
}

static uint64_t
btrfs_ip_objectid(struct inode *ip)
{
  return ((uint64_t)ip->addrs[BTRFS_IP_OBJECTID_HI] << 32) |
         (uint64_t)ip->inum;
}

static uint64_t
btrfs_ip_rootdir(struct inode *ip)
{
  return ((uint64_t)ip->addrs[BTRFS_IP_ROOTDIR_HI] << 32) |
         (uint64_t)ip->addrs[BTRFS_IP_ROOTDIR_LO];
}

static uint64_t
btrfs_ip_treeid(struct inode *ip)
{
  return ((uint64_t)ip->addrs[BTRFS_IP_TREEID_HI] << 32) |
         (uint64_t)ip->addrs[BTRFS_IP_TREEID_LO];
}

static int
btrfs_read_phys(struct btrfs_mount_data *md, uint64_t phys, void *dst, uint32_t len)
{
  uint32_t done;

  if(md == 0 || dst == 0)
    return -1;

  done = 0;
  while(done < len){
    uint64_t cur;
    uint64_t block64;
    uint32_t off;
    uint32_t chunk;
    struct buf *bp;

    cur = phys + done;
    block64 = cur / BSIZE;
    off = (uint32_t)(cur % BSIZE);
    chunk = BSIZE - off;
    if(chunk > len - done)
      chunk = len - done;
    if(block64 > 0xFFFFFFFFULL)
      return -1;
    if(bread_ok(md->dev, (uint)block64, &bp) < 0)
      return -1;
    memmove((char*)dst + done, bp->data + off, chunk);
    brelse(bp);
    done += chunk;
  }

  return 0;
}

static struct btrfs_chunk_map*
btrfs_find_chunk(struct btrfs_mount_data *md, uint64_t logical)
{
  int i;

  if(md == 0)
    return 0;
  for(i = 0; i < md->map_count; i++){
    struct btrfs_chunk_map *map;

    map = &md->maps[i];
    if(!map->valid)
      continue;
    if(logical >= map->logical && logical < map->logical + map->length)
      return map;
  }
  return 0;
}

static int
btrfs_read_logical(struct btrfs_mount_data *md, uint64_t logical,
                   void *dst, uint32_t len)
{
  uint32_t done;

  if(md == 0 || dst == 0)
    return -1;

  done = 0;
  while(done < len){
    struct btrfs_chunk_map *map;
    uint64_t cur;
    uint64_t delta;
    uint64_t avail;
    uint32_t chunk;

    cur = logical + done;
    map = btrfs_find_chunk(md, cur);
    if(map == 0)
      return -1;
    delta = cur - map->logical;
    avail = map->length - delta;
    chunk = (avail > (uint64_t)(len - done)) ? (len - done) : (uint32_t)avail;
    if(btrfs_read_phys(md, map->physical + delta, (char*)dst + done, chunk) < 0)
      return -1;
    done += chunk;
  }

  return 0;
}

static int
btrfs_add_chunk_map(struct btrfs_mount_data *md, uint64_t logical,
                    struct btrfs_chunk *chunk)
{
  int i;
  uint16_t stripes;
  uint64_t physical;
  uint64_t devid;

  if(md == 0 || chunk == 0)
    return -1;

  stripes = btrfs_get_u16(&chunk->num_stripes);
  if(stripes == 0)
    return -1;

  physical = btrfs_get_u64(&chunk->stripe.offset);
  devid = btrfs_get_u64(&chunk->stripe.devid);
  for(i = 1; i < stripes; i++){
    struct btrfs_stripe *stripe;

    stripe = (struct btrfs_stripe *)((char*)chunk + offsetof(struct btrfs_chunk, stripe) +
                                     i * sizeof(struct btrfs_stripe));
    if(btrfs_get_u64(&stripe->devid) != devid)
      return -1;
  }

  for(i = 0; i < md->map_count; i++){
    if(md->maps[i].valid && md->maps[i].logical == logical)
      return 0;
  }
  if(md->map_count >= BTRFS_MAX_CHUNK_MAPS)
    return -1;

  md->maps[md->map_count].logical = logical;
  md->maps[md->map_count].length = btrfs_get_u64(&chunk->length);
  md->maps[md->map_count].physical = physical;
  md->maps[md->map_count].devid = devid;
  md->maps[md->map_count].flags = btrfs_get_u64(&chunk->type);
  md->maps[md->map_count].stripes = stripes;
  md->maps[md->map_count].valid = 1;
  md->map_count++;
  return 0;
}

static int
btrfs_parse_sys_chunk_array(struct btrfs_mount_data *md)
{
  uint32_t off;
  uint32_t size;

  if(md == 0)
    return -1;

  off = 0;
  size = md->sb.sys_chunk_array_size;
  while(off < size){
    struct btrfs_disk_key *dk;
    struct btrfs_key key;
    struct btrfs_chunk *chunk;
    uint64_t item_len;

    if(size - off < sizeof(*dk))
      return -1;
    dk = (struct btrfs_disk_key *)(md->sb.sys_chunk_array + off);
    key = btrfs_disk_key_to_cpu(dk);
    off += sizeof(*dk);

    if(key.type != BTRFS_CHUNK_ITEM_KEY)
      return -1;
    if(size - off < offsetof(struct btrfs_chunk, stripe) + sizeof(struct btrfs_stripe))
      return -1;
    chunk = (struct btrfs_chunk *)(md->sb.sys_chunk_array + off);
    item_len = btrfs_chunk_item_size(btrfs_get_u16(&chunk->num_stripes));
    if(item_len > size - off)
      return -1;
    if(btrfs_add_chunk_map(md, key.offset, chunk) < 0)
      return -1;
    off += item_len;
  }

  return 0;
}

static void*
btrfs_alloc_tree_block(struct btrfs_mount_data *md)
{
  if(md == 0)
    return 0;
  if(md->sb.nodesize > PGSIZE)
    return 0;
  return kalloc();
}

static int
btrfs_read_tree_block(struct btrfs_mount_data *md, uint64_t bytenr, void *buf)
{
  struct btrfs_header *hdr;

  if(md == 0 || buf == 0)
    return -1;
  if(btrfs_read_logical(md, bytenr, buf, md->sb.nodesize) < 0)
    return -1;
  hdr = (struct btrfs_header*)buf;
  if(btrfs_get_u64(&hdr->bytenr) != bytenr)
    return -1;
  return 0;
}

static int
btrfs_walk_tree(struct btrfs_mount_data *md, uint64_t bytenr, uchar level,
                btrfs_item_cb cb, void *arg)
{
  void *buf;
  struct btrfs_header *hdr;
  uint32_t nritems;
  int ret;
  uint32_t i;

  if(md == 0 || cb == 0)
    return -1;

  buf = btrfs_alloc_tree_block(md);
  if(buf == 0)
    return -1;
  if(btrfs_read_tree_block(md, bytenr, buf) < 0){
    kfree(buf);
    return -1;
  }

  hdr = (struct btrfs_header*)buf;
  if(hdr->level != level){
    kfree(buf);
    return -1;
  }

  nritems = btrfs_get_u32(&hdr->nritems);
  if(level == 0){
    uint32_t leaf_limit;

    leaf_limit = md->sb.nodesize - offsetof(struct btrfs_header, level) - sizeof(hdr->level);
    for(i = 0; i < nritems; i++){
      struct btrfs_item *item;
      struct btrfs_key key;
      uint32_t item_off;
      uint32_t item_size;
      void *item_ptr;

      item = (struct btrfs_item *)((char*)buf + offsetof(struct btrfs_header, level) + sizeof(hdr->level) +
                                   i * sizeof(struct btrfs_item));
      key = btrfs_disk_key_to_cpu(&item->key);
      item_off = btrfs_get_u32(&item->offset);
      item_size = btrfs_get_u32(&item->size);
      if(item_off > leaf_limit || item_size > leaf_limit || item_off + item_size > leaf_limit){
        kfree(buf);
        return -1;
      }
      item_ptr = (char*)buf + offsetof(struct btrfs_header, level) + sizeof(hdr->level) + item_off;
      ret = cb(md, buf, &key, item_ptr, item_size, arg);
      if(ret != 0){
        kfree(buf);
        return ret;
      }
    }
  } else {
    for(i = 0; i < nritems; i++){
      struct btrfs_key_ptr *ptr;
      uint64_t child;

      ptr = (struct btrfs_key_ptr *)((char*)buf + offsetof(struct btrfs_key_ptr, key) +
                                     offsetof(struct btrfs_header, level) + sizeof(hdr->level) +
                                     i * sizeof(struct btrfs_key_ptr));
      child = btrfs_get_u64(&ptr->blockptr);
      ret = btrfs_walk_tree(md, child, level - 1, cb, arg);
      if(ret != 0){
        kfree(buf);
        return ret;
      }
    }
  }

  kfree(buf);
  return 0;
}

struct btrfs_find_chunk_ctx {
  int error;
};

static int
btrfs_scan_chunk_cb(struct btrfs_mount_data *md, void *leaf, struct btrfs_key *key,
                    void *item_ptr, uint32_t item_size, void *arg)
{
  struct btrfs_chunk *chunk;
  struct btrfs_find_chunk_ctx *ctx;

  (void)leaf;
  ctx = (struct btrfs_find_chunk_ctx*)arg;
  if(key->objectid != BTRFS_FIRST_CHUNK_TREE_OBJECTID || key->type != BTRFS_CHUNK_ITEM_KEY)
    return 0;
  if(item_size < offsetof(struct btrfs_chunk, stripe) + sizeof(struct btrfs_stripe)){
    ctx->error = -1;
    return -1;
  }
  chunk = (struct btrfs_chunk*)item_ptr;
  if(btrfs_chunk_item_size(btrfs_get_u16(&chunk->num_stripes)) > item_size){
    ctx->error = -1;
    return -1;
  }
  if(btrfs_add_chunk_map(md, key->offset, chunk) < 0){
    ctx->error = -1;
    return -1;
  }
  return 0;
}

struct btrfs_root_lookup_ctx {
  uint64_t objectid;
  uint64_t best_offset;
  struct btrfs_root_item item;
  int found;
};

static int
btrfs_scan_root_item_cb(struct btrfs_mount_data *md, void *leaf, struct btrfs_key *key,
                        void *item_ptr, uint32_t item_size, void *arg)
{
  struct btrfs_root_lookup_ctx *ctx;

  (void)md;
  (void)leaf;
  ctx = (struct btrfs_root_lookup_ctx*)arg;
  if(key->objectid != ctx->objectid || key->type != BTRFS_ROOT_ITEM_KEY)
    return 0;
  if(item_size < offsetof(struct btrfs_root_item, level) + sizeof(((struct btrfs_root_item*)0)->level))
    return -1;
  if(!ctx->found || key->offset >= ctx->best_offset){
    memset(&ctx->item, 0, sizeof(ctx->item));
    if(item_size > sizeof(ctx->item))
      item_size = sizeof(ctx->item);
    memmove(&ctx->item, item_ptr, item_size);
    ctx->best_offset = key->offset;
    ctx->found = 1;
  }
  return 0;
}

static int
btrfs_lookup_root_item(struct btrfs_mount_data *md, uint64_t objectid,
                       struct btrfs_root_item *out)
{
  struct btrfs_root_lookup_ctx ctx;

  if(md == 0 || out == 0)
    return -1;
  memset(&ctx, 0, sizeof(ctx));
  ctx.objectid = objectid;
  if(btrfs_walk_tree(md, md->sb.root, md->sb.root_level, btrfs_scan_root_item_cb, &ctx) < 0)
    return -1;
  if(!ctx.found)
    return -1;
  memmove(out, &ctx.item, sizeof(*out));
  return 0;
}

struct btrfs_inode_lookup_ctx {
  uint64_t objectid;
  struct btrfs_inode_item item;
  int found;
};

static int
btrfs_scan_inode_item_cb(struct btrfs_mount_data *md, void *leaf, struct btrfs_key *key,
                         void *item_ptr, uint32_t item_size, void *arg)
{
  struct btrfs_inode_lookup_ctx *ctx;

  (void)md;
  (void)leaf;
  ctx = (struct btrfs_inode_lookup_ctx*)arg;
  if(key->objectid != ctx->objectid || key->type != BTRFS_INODE_ITEM_KEY || key->offset != 0)
    return 0;
  if(item_size < sizeof(ctx->item))
    return -1;
  memmove(&ctx->item, item_ptr, sizeof(ctx->item));
  ctx->found = 1;
  return 1;
}

static int
btrfs_lookup_inode_item(struct btrfs_mount_data *md, uint64_t tree_bytenr,
                        uchar tree_level, uint64_t objectid,
                        struct btrfs_inode_item *out)
{
  struct btrfs_inode_lookup_ctx ctx;

  if(md == 0 || out == 0)
    return -1;
  memset(&ctx, 0, sizeof(ctx));
  ctx.objectid = objectid;
  if(btrfs_walk_tree(md, tree_bytenr, tree_level, btrfs_scan_inode_item_cb, &ctx) < 0 && !ctx.found)
    return -1;
  if(!ctx.found)
    return -1;
  memmove(out, &ctx.item, sizeof(*out));
  return 0;
}

struct btrfs_parent_lookup_ctx {
  uint64_t objectid;
  uint64_t parent;
  int found;
};

static int
btrfs_scan_parent_cb(struct btrfs_mount_data *md, void *leaf, struct btrfs_key *key,
                     void *item_ptr, uint32_t item_size, void *arg)
{
  struct btrfs_parent_lookup_ctx *ctx;

  (void)md;
  (void)leaf;
  (void)item_ptr;
  (void)item_size;
  ctx = (struct btrfs_parent_lookup_ctx*)arg;
  if(key->objectid != ctx->objectid)
    return 0;
  if(key->type != BTRFS_INODE_REF_KEY && key->type != BTRFS_INODE_EXTREF_KEY)
    return 0;
  if(key->type == BTRFS_INODE_REF_KEY){
    ctx->parent = key->offset;
    ctx->found = 1;
    return 1;
  }
  if(item_size >= sizeof(uint64_t) * 2 + sizeof(uint16_t)){
    struct btrfs_inode_extref *extref;

    extref = (struct btrfs_inode_extref*)item_ptr;
    ctx->parent = btrfs_get_u64(&extref->parent_objectid);
    ctx->found = 1;
    return 1;
  }
  return -1;
}

static int
btrfs_lookup_parent_objectid(struct btrfs_mount_data *md, uint64_t tree_bytenr,
                             uchar tree_level, uint64_t objectid,
                             uint64_t root_dirid, uint64_t *parent)
{
  struct btrfs_parent_lookup_ctx ctx;

  if(md == 0 || parent == 0)
    return -1;
  if(objectid == root_dirid){
    *parent = root_dirid;
    return 0;
  }
  memset(&ctx, 0, sizeof(ctx));
  ctx.objectid = objectid;
  if(btrfs_walk_tree(md, tree_bytenr, tree_level, btrfs_scan_parent_cb, &ctx) < 0 && !ctx.found)
    return -1;
  if(!ctx.found)
    return -1;
  *parent = ctx.parent;
  return 0;
}

static short
btrfs_mode_to_type(uint32_t mode)
{
  switch(mode & M_IFMT){
  case M_IFDIR:
    return T_DIR;
  case M_IFLNK:
    return T_SYMLINK;
  case M_IFCHR:
  case M_IFBLK:
    return T_DEV;
  case M_IFREG:
  default:
    return T_FILE;
  }
}

static struct inode*
btrfs_make_inode(struct btrfs_mount_data *md, uint64_t tree_id,
                 uint64_t tree_bytenr, uchar tree_level,
                 uint64_t root_dirid, uint64_t objectid,
                 struct btrfs_inode_item *ii)
{
  struct inode *ip;
  uint32_t mode;
  uint64_t size64;

  if(md == 0 || ii == 0)
    return 0;
  if((objectid >> 32) > 0xFFFFFFFFULL)
    return 0;

  ip = iget(md->dev, (uint)objectid);
  if(ip == 0)
    return 0;

  acquiresleep(&ip->lock);
  mode = btrfs_get_u32(&ii->mode);
  size64 = btrfs_get_u64(&ii->size);

  ip->type = btrfs_mode_to_type(mode);
  ip->major = 0;
  ip->minor = 0;
  ip->nlink = (short)btrfs_get_u32(&ii->nlink);
  ip->uid = (short)btrfs_get_u32(&ii->uid);
  ip->gid = (short)btrfs_get_u32(&ii->gid);
  ip->mode = (short)(mode & 0xFFFF);
  ip->size = size64;  /* uint64_t: no longer clamped to 4 GB */

  ip->addrs[BTRFS_IP_TREE_BYTENR_LO] = (uint)(tree_bytenr & 0xFFFFFFFFULL);
  ip->addrs[BTRFS_IP_TREE_BYTENR_HI] = (uint)(tree_bytenr >> 32);
  ip->addrs[BTRFS_IP_TREE_LEVEL] = tree_level;
  ip->addrs[BTRFS_IP_OBJECTID_HI] = (uint)(objectid >> 32);
  ip->addrs[BTRFS_IP_ROOTDIR_LO] = (uint)(root_dirid & 0xFFFFFFFFULL);
  ip->addrs[BTRFS_IP_ROOTDIR_HI] = (uint)(root_dirid >> 32);
  ip->addrs[BTRFS_IP_TREEID_LO] = (uint)(tree_id & 0xFFFFFFFFULL);
  ip->addrs[BTRFS_IP_TREEID_HI] = (uint)(tree_id >> 32);
  ip->valid = 1;

  releasesleep(&ip->lock);
  return ip;
}

static int
btrfs_load_root_inode(struct btrfs_mount_data *md, uint64_t tree_id,
                      uint64_t tree_bytenr, uchar tree_level,
                      uint64_t root_dirid, struct inode **out)
{
  struct btrfs_inode_item ii;
  uint64_t dirid;

  if(md == 0 || out == 0)
    return -1;

  dirid = root_dirid ? root_dirid : BTRFS_FIRST_FREE_OBJECTID;
  if(btrfs_lookup_inode_item(md, tree_bytenr, tree_level, dirid, &ii) < 0)
    return -1;
  *out = btrfs_make_inode(md, tree_id, tree_bytenr, tree_level, dirid, dirid, &ii);
  return (*out == 0) ? -1 : 0;
}

struct btrfs_dir_lookup_ctx {
  uint64_t dirid;
  char *name;
  int want_type;
  int saw_type;
  struct btrfs_key location;
  uchar ftype;
  int found;
};

static int
btrfs_dir_item_match(void *item_ptr, uint32_t item_size, char *name,
                     struct btrfs_key *location, uchar *ftype)
{
  uint32_t off;

  if(item_ptr == 0 || name == 0 || location == 0 || ftype == 0)
    return 0;

  off = 0;
  while(off + sizeof(struct btrfs_dir_item) <= item_size){
    struct btrfs_dir_item *di;
    uint16_t name_len;
    uint16_t data_len;
    uint32_t total;

    di = (struct btrfs_dir_item *)((char*)item_ptr + off);
    name_len = btrfs_get_u16(&di->name_len);
    data_len = btrfs_get_u16(&di->data_len);
    total = sizeof(*di) + name_len + data_len;
    if(total > item_size - off)
      return 0;
    if(strlen(name) == name_len &&
       memcmp((char*)(di + 1), name, name_len) == 0){
      *location = btrfs_disk_key_to_cpu(&di->location);
      *ftype = di->type;
      return 1;
    }
    off += total;
  }
  return 0;
}

static int
btrfs_scan_dir_lookup_cb(struct btrfs_mount_data *md, void *leaf, struct btrfs_key *key,
                         void *item_ptr, uint32_t item_size, void *arg)
{
  struct btrfs_dir_lookup_ctx *ctx;

  (void)md;
  (void)leaf;
  ctx = (struct btrfs_dir_lookup_ctx*)arg;
  if(key->objectid != ctx->dirid)
    return 0;
  if(key->type != ctx->want_type)
    return 0;
  ctx->saw_type = 1;
  if(btrfs_dir_item_match(item_ptr, item_size, ctx->name, &ctx->location, &ctx->ftype)){
    ctx->found = 1;
    return 1;
  }
  return 0;
}

static int
btrfs_lookup_dir_entry(struct btrfs_mount_data *md, uint64_t tree_bytenr,
                       uchar tree_level, uint64_t dirid, char *name,
                       struct btrfs_key *location, uchar *ftype)
{
  struct btrfs_dir_lookup_ctx ctx;
  int ret;

  if(md == 0 || name == 0 || location == 0 || ftype == 0)
    return -1;

  memset(&ctx, 0, sizeof(ctx));
  ctx.dirid = dirid;
  ctx.name = name;
  ctx.want_type = BTRFS_DIR_INDEX_KEY;
  ret = btrfs_walk_tree(md, tree_bytenr, tree_level, btrfs_scan_dir_lookup_cb, &ctx);
  if(ctx.found){
    *location = ctx.location;
    *ftype = ctx.ftype;
    return 0;
  }
  if(ret < 0 && !ctx.saw_type)
    return -1;

  memset(&ctx, 0, sizeof(ctx));
  ctx.dirid = dirid;
  ctx.name = name;
  ctx.want_type = BTRFS_DIR_ITEM_KEY;
  ret = btrfs_walk_tree(md, tree_bytenr, tree_level, btrfs_scan_dir_lookup_cb, &ctx);
  if(ctx.found){
    *location = ctx.location;
    *ftype = ctx.ftype;
    return 0;
  }
  return -1;
}

struct btrfs_nth_lookup_ctx {
  uint64_t dirid;
  uint want;
  uint cur;
  int want_type;
  int saw_type;
  char name[DIRSIZ];
  struct btrfs_key location;
  uchar ftype;
  int found;
};

static int
btrfs_scan_dir_nth_cb(struct btrfs_mount_data *md, void *leaf, struct btrfs_key *key,
                      void *item_ptr, uint32_t item_size, void *arg)
{
  struct btrfs_nth_lookup_ctx *ctx;
  uint32_t off;

  (void)md;
  (void)leaf;
  ctx = (struct btrfs_nth_lookup_ctx*)arg;
  if(key->objectid != ctx->dirid || key->type != ctx->want_type)
    return 0;
  ctx->saw_type = 1;

  off = 0;
  while(off + sizeof(struct btrfs_dir_item) <= item_size){
    struct btrfs_dir_item *di;
    uint16_t name_len;
    uint16_t data_len;
    uint32_t total;
    int ncopy;

    di = (struct btrfs_dir_item *)((char*)item_ptr + off);
    name_len = btrfs_get_u16(&di->name_len);
    data_len = btrfs_get_u16(&di->data_len);
    total = sizeof(*di) + name_len + data_len;
    if(total > item_size - off)
      return 0;
    if(ctx->cur == ctx->want){
      ctx->location = btrfs_disk_key_to_cpu(&di->location);
      ctx->ftype = di->type;
      memset(ctx->name, 0, sizeof(ctx->name));
      ncopy = name_len;
      if(ncopy >= DIRSIZ)
        ncopy = DIRSIZ - 1;
      memmove(ctx->name, di + 1, ncopy);
      ctx->name[ncopy] = 0;
      ctx->found = 1;
      return 1;
    }
    ctx->cur++;
    off += total;
  }
  return 0;
}

static int
btrfs_lookup_nth_dir_entry(struct btrfs_mount_data *md, uint64_t tree_bytenr,
                           uchar tree_level, uint64_t dirid, uint want,
                           char *name, struct btrfs_key *location,
                           uchar *ftype)
{
  struct btrfs_nth_lookup_ctx ctx;

  if(md == 0 || name == 0 || location == 0 || ftype == 0)
    return -1;

  memset(&ctx, 0, sizeof(ctx));
  ctx.dirid = dirid;
  ctx.want = want;
  ctx.want_type = BTRFS_DIR_INDEX_KEY;
  btrfs_walk_tree(md, tree_bytenr, tree_level, btrfs_scan_dir_nth_cb, &ctx);
  if(ctx.found){
    safestrcpy(name, ctx.name, DIRSIZ);
    *location = ctx.location;
    *ftype = ctx.ftype;
    return 0;
  }

  if(ctx.saw_type)
    return -1;

  memset(&ctx, 0, sizeof(ctx));
  ctx.dirid = dirid;
  ctx.want = want;
  ctx.want_type = BTRFS_DIR_ITEM_KEY;
  btrfs_walk_tree(md, tree_bytenr, tree_level, btrfs_scan_dir_nth_cb, &ctx);
  if(ctx.found){
    safestrcpy(name, ctx.name, DIRSIZ);
    *location = ctx.location;
    *ftype = ctx.ftype;
    return 0;
  }
  return -1;
}

static struct inode*
btrfs_load_location_inode(struct btrfs_mount_data *md, uint64_t cur_tree_id,
                          uint64_t cur_tree_bytenr, uchar cur_tree_level,
                          uint64_t cur_root_dirid, struct btrfs_key *location)
{
  struct btrfs_inode_item ii;
  struct btrfs_root_item ri;
  uint64_t root_dirid;

  if(md == 0 || location == 0)
    return 0;

  if(location->type == BTRFS_INODE_ITEM_KEY){
    if(btrfs_lookup_inode_item(md, cur_tree_bytenr, cur_tree_level,
                               location->objectid, &ii) < 0)
      return 0;
    return btrfs_make_inode(md, cur_tree_id, cur_tree_bytenr, cur_tree_level,
                            cur_root_dirid, location->objectid, &ii);
  }

  if(location->type != BTRFS_ROOT_ITEM_KEY)
    return 0;
  if(btrfs_lookup_root_item(md, location->objectid, &ri) < 0)
    return 0;
  root_dirid = btrfs_get_u64(&ri.root_dirid);
  if(root_dirid == 0)
    root_dirid = BTRFS_FIRST_FREE_OBJECTID;
  if(btrfs_lookup_inode_item(md, btrfs_get_u64(&ri.bytenr), ri.level,
                             root_dirid, &ii) < 0)
    return 0;
  return btrfs_make_inode(md, location->objectid, btrfs_get_u64(&ri.bytenr),
                          ri.level, root_dirid, root_dirid, &ii);
}

struct btrfs_read_ctx {
  struct inode *ip;
  uint64_t off;
  uint64_t end;
  uint64_t cursor;
  char *dst;
  uint32_t done;
  int error;
};

static void
btrfs_fill_zeros(struct btrfs_read_ctx *ctx, uint64_t upto)
{
  uint64_t span;

  if(ctx == 0)
    return;
  if(upto <= ctx->cursor)
    return;
  if(upto > ctx->end)
    upto = ctx->end;
  span = upto - ctx->cursor;
  if(span > 0){
    memset(ctx->dst + ctx->done, 0, (uint)span);
    ctx->done += (uint)span;
    ctx->cursor = upto;
  }
}

static int
btrfs_scan_file_extent_cb(struct btrfs_mount_data *md, void *leaf, struct btrfs_key *key,
                          void *item_ptr, uint32_t item_size, void *arg)
{
  struct btrfs_read_ctx *ctx;
  struct inode *ip;

  (void)leaf;
  ctx = (struct btrfs_read_ctx*)arg;
  ip = ctx->ip;
  if(key->objectid != btrfs_ip_objectid(ip) || key->type != BTRFS_EXTENT_DATA_KEY)
    return 0;

  if(key->offset > ctx->cursor)
    btrfs_fill_zeros(ctx, key->offset);
  if(ctx->cursor >= ctx->end)
    return 1;

  if(item_size < offsetof(struct btrfs_file_extent_item, disk_bytenr))
    return -1;

  {
    struct btrfs_file_extent_item *fi;
    uint64_t extent_start;
    uint64_t extent_len;
    uint64_t copy_start;
    uint64_t copy_end;
    uint64_t within;
    uint32_t chunk;

    fi = (struct btrfs_file_extent_item*)item_ptr;
    if(fi->compression != 0 || fi->encryption != 0 || btrfs_get_u16(&fi->other_encoding) != 0)
      return -1;

    extent_start = key->offset;
    extent_len = btrfs_get_u64(&fi->num_bytes);
    if(fi->type == BTRFS_FILE_EXTENT_INLINE)
      extent_len = btrfs_get_u64(&fi->ram_bytes);
    if(extent_len == 0)
      return 0;

    copy_start = (ctx->cursor > extent_start) ? ctx->cursor : extent_start;
    copy_end = extent_start + extent_len;
    if(copy_end > ctx->end)
      copy_end = ctx->end;
    if(copy_end <= copy_start)
      return 0;

    within = copy_start - extent_start;
    chunk = (uint32_t)(copy_end - copy_start);

    if(fi->type == BTRFS_FILE_EXTENT_INLINE){
      uint32_t inline_len;

      inline_len = item_size - offsetof(struct btrfs_file_extent_item, disk_bytenr);
      if(within + chunk > inline_len)
        return -1;
      memmove(ctx->dst + ctx->done,
              (char*)item_ptr + offsetof(struct btrfs_file_extent_item, disk_bytenr) + within,
              chunk);
    } else if(fi->type == BTRFS_FILE_EXTENT_REG || fi->type == BTRFS_FILE_EXTENT_PREALLOC){
      uint64_t disk_bytenr;

      disk_bytenr = btrfs_get_u64(&fi->disk_bytenr);
      if(disk_bytenr == 0){
        memset(ctx->dst + ctx->done, 0, chunk);
      } else {
        uint64_t logical;

        logical = disk_bytenr + btrfs_get_u64(&fi->offset) + within;
        if(btrfs_read_logical(md, logical, ctx->dst + ctx->done, chunk) < 0)
          return -1;
      }
    } else {
      return -1;
    }

    ctx->done += chunk;
    ctx->cursor = copy_end;
    if(ctx->cursor >= ctx->end)
      return 1;
  }

  return 0;
}

static int
btrfs_read_file_bytes(struct inode *ip, char *dst, uint off, uint n)
{
  struct btrfs_mount_data *md;
  struct btrfs_read_ctx ctx;
  int ret;

  if(ip == 0 || dst == 0)
    return -1;
  md = btrfs_data_for_dev(ip->dev);
  if(md == 0)
    return -1;
  if(off >= ip->size)
    return 0;
  if(off + n < off)
    return -1;
  if(off + n > ip->size)
    n = ip->size - off;

  memset(&ctx, 0, sizeof(ctx));
  ctx.ip = ip;
  ctx.off = off;
  ctx.end = (uint64_t)off + n;
  ctx.cursor = off;
  ctx.dst = dst;

  ret = btrfs_walk_tree(md, btrfs_ip_tree_bytenr(ip), (uchar)ip->addrs[BTRFS_IP_TREE_LEVEL],
                        btrfs_scan_file_extent_cb, &ctx);
  if(ret < 0 && ret != 1)
    return -1;
  if(ctx.cursor < ctx.end)
    btrfs_fill_zeros(&ctx, ctx.end);
  return ctx.done;
}

static struct inode*
btrfs_root_inode(struct vfs *fs)
{
  struct btrfs_mount_data *md;
  struct inode *ip;

  md = fs ? (struct btrfs_mount_data*)fs->fs_data : 0;
  if(md == 0)
    return 0;
  ip = 0;
  if(btrfs_load_root_inode(md, md->mounted_tree_id, md->mounted_tree_bytenr,
                           md->mounted_tree_level, md->mounted_root_dirid, &ip) < 0)
    return 0;
  return ip;
}

static int
btrfs_read(struct inode *ip, char *dst, uint off, uint n)
{
  uint idx;
  uint written;

  if(ip == 0 || dst == 0)
    return -1;

  if(ip->type != T_DIR)
    return btrfs_read_file_bytes(ip, dst, off, n);

  if(n < sizeof(struct dirent) || (off % sizeof(struct dirent)) != 0)
    return 0;

  idx = off / sizeof(struct dirent);
  written = 0;
  while(written + sizeof(struct dirent) <= n){
    struct dirent de;
    struct btrfs_mount_data *md;
    uint64_t parent;
    char name[DIRSIZ];
    struct btrfs_key location;
    uchar ftype;
    int ret;

    memset(&de, 0, sizeof(de));
    md = btrfs_data_for_dev(ip->dev);
    if(md == 0)
      return (written > 0) ? (int)written : -1;

    if(idx == 0){
      de.inum = (ushort)(ip->inum & 0xFFFF);
      if(de.inum == 0)
        de.inum = 1;
      de.name[0] = '.';
    } else if(idx == 1){
      if(btrfs_lookup_parent_objectid(md, btrfs_ip_tree_bytenr(ip),
                                      (uchar)ip->addrs[BTRFS_IP_TREE_LEVEL],
                                      btrfs_ip_objectid(ip), btrfs_ip_rootdir(ip),
                                      &parent) < 0)
        parent = btrfs_ip_rootdir(ip);
      de.inum = (ushort)(parent & 0xFFFF);
      if(de.inum == 0)
        de.inum = 1;
      de.name[0] = '.';
      de.name[1] = '.';
    } else {
      ret = btrfs_lookup_nth_dir_entry(md, btrfs_ip_tree_bytenr(ip),
                                       (uchar)ip->addrs[BTRFS_IP_TREE_LEVEL],
                                       btrfs_ip_objectid(ip), idx - 2,
                                       name, &location, &ftype);
      if(ret < 0)
        break;
      de.inum = (ushort)(location.objectid & 0xFFFF);
      if(de.inum == 0)
        de.inum = 1;
      safestrcpy(de.name, name, sizeof(de.name));
      (void)ftype;
    }

    memmove(dst + written, &de, sizeof(de));
    written += sizeof(de);
    idx++;
  }

  return written;
}

static int
btrfs_write(struct inode *ip, char *src, uint off, uint n)
{
  (void)ip;
  (void)src;
  (void)off;
  (void)n;
  return -1;
}

static int
btrfs_truncate(struct inode *ip)
{
  (void)ip;
  return -1;
}

static int
btrfs_stat(struct inode *ip, struct stat *st)
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

static struct inode*
btrfs_dirlookup(struct inode *dp, char *name, uint *poff)
{
  struct btrfs_mount_data *md;
  uint64_t parent;
  struct btrfs_key location;
  uchar ftype;

  if(dp == 0 || name == 0)
    return 0;
  if(dp->type != T_DIR)
    return 0;

  if(namecmp(name, ".") == 0)
    return idup(dp);

  md = btrfs_data_for_dev(dp->dev);
  if(md == 0)
    return 0;

  if(namecmp(name, "..") == 0){
    struct btrfs_inode_item ii;

    if(btrfs_lookup_parent_objectid(md, btrfs_ip_tree_bytenr(dp),
                                    (uchar)dp->addrs[BTRFS_IP_TREE_LEVEL],
                                    btrfs_ip_objectid(dp), btrfs_ip_rootdir(dp),
                                    &parent) < 0)
      parent = btrfs_ip_rootdir(dp);
    if(btrfs_lookup_inode_item(md, btrfs_ip_tree_bytenr(dp),
                               (uchar)dp->addrs[BTRFS_IP_TREE_LEVEL],
                               parent, &ii) < 0)
      return idup(dp);
    return btrfs_make_inode(md, btrfs_ip_treeid(dp), btrfs_ip_tree_bytenr(dp),
                            (uchar)dp->addrs[BTRFS_IP_TREE_LEVEL],
                            btrfs_ip_rootdir(dp), parent, &ii);
  }

  if(btrfs_lookup_dir_entry(md, btrfs_ip_tree_bytenr(dp),
                            (uchar)dp->addrs[BTRFS_IP_TREE_LEVEL],
                            btrfs_ip_objectid(dp), name, &location,
                            &ftype) < 0)
    return 0;
  if(poff)
    *poff = 0;
  return btrfs_load_location_inode(md, btrfs_ip_treeid(dp),
                                   btrfs_ip_tree_bytenr(dp),
                                   (uchar)dp->addrs[BTRFS_IP_TREE_LEVEL],
                                   btrfs_ip_rootdir(dp), &location);
}

static struct inode*
btrfs_create(struct inode *dp, char *name, short type,
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

static int btrfs_dirlink(struct inode *dp, char *name, uint inum)
{
  (void)dp;
  (void)name;
  (void)inum;
  return -1;
}

static int btrfs_link(struct inode *ip, struct inode *dp, char *name)
{
  (void)ip;
  (void)dp;
  (void)name;
  return -1;
}

static int btrfs_remove(struct inode *dp, char *name)
{
  (void)dp;
  (void)name;
  return -1;
}

static int btrfs_rename(struct inode *olddp, char *oldname,
                        struct inode *newdp, char *newname)
{
  (void)olddp;
  (void)oldname;
  (void)newdp;
  (void)newname;
  return -1;
}

static int btrfs_setattr(struct inode *ip, int set_mode, int mode,
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

static int btrfs_access(struct inode *ip, int mode)
{
  (void)ip;
  return (mode & IACC_WRITE) ? -1 : 0;
}

static int
btrfs_readlink(struct inode *ip, char *buf, uint size)
{
  int n;

  if(ip == 0 || buf == 0 || size == 0 || ip->type != T_SYMLINK)
    return -1;
  n = btrfs_read_file_bytes(ip, buf, 0, size);
  if(n < 0)
    return -1;
  return n;
}

static char*
btrfs_skipelem(char *path, char *name)
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
btrfs_walk(struct vfs *fs, char *path, int nameiparent, char *name)
{
  struct inode *ip;
  struct btrfs_mount_data *md;
  char elem[DIRSIZ + 1];
  char *p;
  int i;

  if(path == 0)
    return 0;
  md = fs ? (struct btrfs_mount_data*)fs->fs_data : 0;
  if(md == 0)
    return 0;

  if(path[0] == '/'){
    ip = btrfs_root_inode(fs);
  } else {
    ip = proc_cwd_idup();
    if(ip == 0 || btrfs_data_for_dev(ip->dev) == 0){
      if(ip)
        iput(ip);
      ip = btrfs_root_inode(fs);
    }
  }
  if(ip == 0)
    return 0;

  p = path;
  while((p = btrfs_skipelem(p, elem)) != 0){
    struct inode *next;

    if(elem[0] == 0 || (elem[0] == '.' && elem[1] == 0))
      continue;
    if(nameiparent && *p == 0){
      if(name){
        for(i = 0; i < DIRSIZ; i++)
          name[i] = 0;
        for(i = 0; elem[i] && i < DIRSIZ; i++)
          name[i] = elem[i];
      }
      return ip;
    }
    next = btrfs_dirlookup(ip, elem, 0);
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
btrfs_namei(struct vfs *fs, char *path)
{
  return btrfs_walk(fs, path, 0, 0);
}

static struct inode*
btrfs_nameiparent(struct vfs *fs, char *path, char *name)
{
  return btrfs_walk(fs, path, 1, name);
}

static void
btrfs_inode_put(struct inode *ip)
{
  iput(ip);
}

static int
btrfs_mount_init(struct mount *m)
{
  struct btrfs_mount_data *md;
  char *buf;
  struct btrfs_root_item root_item;
  struct btrfs_key location;
  uchar ftype;
  uint64_t tree_id;

  if(m == 0)
    return -1;
  if(bdev_nblocks(m->dev) == 0)
    return -1;

  md = (struct btrfs_mount_data*)kalloc();
  if(md == 0)
    return -1;
  memset(md, 0, sizeof(*md));
  initlock(&md->lock, "btrfs");
  lockdep_set_rank(&md->lock, LOCK_RANK_DEFAULT, "btrfs");
  md->dev = m->dev;

  buf = kalloc();
  if(buf == 0){
    kfree((char*)md);
    return -1;
  }
  if(btrfs_read_phys(md, BTRFS_SUPER_INFO_OFFSET, buf, BTRFS_SUPER_INFO_SIZE) < 0)
    goto fail;
  memmove(&md->sb, buf, sizeof(md->sb));

  if(md->sb.magic != BTRFS_MAGIC)
    goto fail;
  if(md->sb.num_devices != 1)
    goto fail;
  if(md->sb.nodesize > PGSIZE || md->sb.nodesize < 4096)
    goto fail;
  if(md->sb.sectorsize != md->sb.nodesize)
    goto fail;
  if(md->sb.root == 0 || md->sb.chunk_root == 0)
    goto fail;
  if(btrfs_parse_sys_chunk_array(md) < 0)
    goto fail;

  {
    struct btrfs_find_chunk_ctx chunk_ctx;

    memset(&chunk_ctx, 0, sizeof(chunk_ctx));
    if(btrfs_walk_tree(md, md->sb.chunk_root, md->sb.chunk_root_level,
                       btrfs_scan_chunk_cb, &chunk_ctx) < 0 && chunk_ctx.error)
      goto fail;
  }

  tree_id = BTRFS_FS_TREE_OBJECTID;
  memset(&location, 0, sizeof(location));
  if(btrfs_lookup_dir_entry(md, md->sb.root, md->sb.root_level,
                            BTRFS_ROOT_TREE_DIR_OBJECTID, "default",
                            &location, &ftype) == 0){
    if(location.type == BTRFS_ROOT_ITEM_KEY)
      tree_id = location.objectid;
  }

  if(btrfs_lookup_root_item(md, tree_id, &root_item) < 0)
    goto fail;

  md->mounted_tree_id = tree_id;
  md->mounted_tree_bytenr = btrfs_get_u64(&root_item.bytenr);
  md->mounted_tree_level = root_item.level;
  md->mounted_root_dirid = btrfs_get_u64(&root_item.root_dirid);
  if(md->mounted_root_dirid == 0)
    md->mounted_root_dirid = BTRFS_FIRST_FREE_OBJECTID;

  m->fs_data = md;
  kfree(buf);
  return 0;

fail:
  kfree(buf);
  kfree((char*)md);
  return -1;
}

static void
btrfs_fs_destroy(struct vfs *fs)
{
  if(fs && fs->fs_data){
    kfree(fs->fs_data);
    fs->fs_data = 0;
  }
}

static struct btrfs_mount_data*
btrfs_data_for_dev(uint dev)
{
  return (struct btrfs_mount_data*)vfs_dev_fs_data(dev);
}

static struct vfs_ops btrfs_vfs_ops = {
  .root_inode = btrfs_root_inode,
  .namei = btrfs_namei,
  .nameiparent = btrfs_nameiparent,
  .inode_put = btrfs_inode_put,
};

static struct vnode_ops btrfs_vnode_ops = {
  .read = btrfs_read,
  .write = btrfs_write,
  .truncate = btrfs_truncate,
  .drop = 0,
  .stat = btrfs_stat,
  .setattr = btrfs_setattr,
  .access = btrfs_access,
  .dirlookup = btrfs_dirlookup,
  .dirlink = btrfs_dirlink,
  .link = btrfs_link,
  .remove = btrfs_remove,
  .rename = btrfs_rename,
  .faultctl = 0,
  .create = btrfs_create,
  .readlink = btrfs_readlink,
  .symlink = 0,
};

void
vfs_btrfs_init(struct vfs *fs)
{
  safestrcpy(fs->name, "btrfs", VFS_NAME_MAX);
  fs->caps = VFS_CAP_READ;
  fs->fs_data = 0;
  fs->fs_destroy = btrfs_fs_destroy;
  fs->mount_init = btrfs_mount_init;
  fs->ops = btrfs_vfs_ops;
  fs->vnode_ops = btrfs_vnode_ops;
}