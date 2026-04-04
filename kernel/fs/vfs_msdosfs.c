#include "types.h"
#include "defs.h"
#include "param.h"
#include "stat.h"
#include "spinlock.h"
#include "sleeplock.h"
#include "date.h"
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
#define MSDOS_ATTR_ARCHIVE 0x20
#define MSDOS_EOC16 0xFFF8
#define MSDOS_EOC32 0x0FFFFFF8

/* Inode number encoding for FAT cluster-chain entries.
 * bits 31:     FAT16 root region flag (1 = FAT16 root slot)
 * bits 30..11: cluster number (20 bits, max ~1M clusters / ~512 MB at 512B/cluster)
 * bits 10..0:  slot within cluster (11 bits, max 2048 entries/cluster)
 */
#define MSDOS_ROOT16_INUM_BASE  0x80000000U   /* OR'd with slot index */
#define MSDOS_CINUM_SLOT_BITS   11
#define MSDOS_CINUM_SLOT_MASK   ((1U << MSDOS_CINUM_SLOT_BITS) - 1)

/* Maximum LFN length (255 characters per VFAT spec) */
#define MSDOS_LFN_MAX_CHARS  255
#define MSDOS_LFN_MAX_SEGS   20     /* ceil(255/13) */

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

struct fat_fsinfo {
  uint lead_sig;
  uchar reserved1[480];
  uint struc_sig;
  uint free_count;
  uint next_free;
  uchar reserved2[12];
  uint trail_sig;
} __attribute__((packed));

/* VFAT Long Filename directory entry (same size as fat_dirent = 32 bytes) */
struct fat_lfn_entry {
  uchar  ord;         /* sequence (1-based) | 0x40 on last (highest-numbered) */
  uchar  name1[10];   /* UTF-16LE codepoints 0-4  (5 chars) */
  uchar  attr;        /* 0x0F = MSDOS_ATTR_LFN */
  uchar  type;        /* 0 */
  uchar  chksum;      /* checksum of 8.3 short name */
  uchar  name2[12];   /* UTF-16LE codepoints 5-10 (6 chars) */
  ushort fclus;       /* 0 */
  uchar  name3[4];    /* UTF-16LE codepoints 11-12 (2 chars) */
} __attribute__((packed));

/* Accumulated LFN state while scanning a directory */
struct fat_lfn_state {
  char  name[MSDOS_LFN_MAX_CHARS + 1];
  int   len;
  uchar chksum;
  int   valid;
};

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
  uint total_clusters;
  uint fsinfo_sector;     /* FAT32: FSInfo sector number (0 = none) */
  uint free_cluster_count; /* FAT32: free cluster count hint (0xFFFFFFFF = unknown) */
};

static uint msdos_active_dev;
static struct msdos_mount_data *msdos_bootstrap_data;

static struct inode* msdos_root_inode(struct vfs *fs);
static struct inode* msdos_dirlookup(struct inode *dp, char *name, uint *poff);
static struct msdos_mount_data* msdos_data_for_dev(uint dev);
static uint msdos_cluster_first_sector(struct msdos_mount_data *md, uint cluster);
static int msdos_fat_set(struct msdos_mount_data *md, uint cluster, uint value);
static int msdos_alloc_cluster(struct msdos_mount_data *md, uint *out);
static int msdos_next_cluster(struct msdos_mount_data *md, uint cluster, uint *next);
static void msdos_update_fsinfo(struct msdos_mount_data *md);
static int msdos_component_to_83(char *name, uchar out[11]);

static struct buf*
msdos_bread(struct msdos_mount_data *md, uint sec)
{
  uint nblocks;
  struct buf *b;

  if(md == 0)
    return 0;
  nblocks = bdev_nblocks(md->dev);
  if(nblocks == 0 || sec >= nblocks){
    cprintf("msdosfs: dev=%d sector out of range sec=%d nblocks=%d\n",
            md->dev, sec, nblocks);
    return 0;
  }
  if(bread_ok(md->dev, sec, &b) < 0)
    return 0;
  return b;
}

static int
msdos_free_cluster_chain(struct msdos_mount_data *md, uint first_cluster)
{
  uint cluster;

  if(md == 0)
    return -1;
  if(first_cluster < 2)
    return 0;

  cluster = first_cluster;
  while(cluster >= 2){
    uint next;

    if(msdos_next_cluster(md, cluster, &next) < 0)
      return -1;
    if(msdos_fat_set(md, cluster, 0) < 0)
      return -1;
    if(md->fat_type == 32 && md->free_cluster_count != 0xFFFFFFFF)
      md->free_cluster_count++;
    if(next == 0)
      break;
    cluster = next;
  }

  msdos_update_fsinfo(md);
  return 0;
}

/* Compute the LFN checksum from an 8.3 short name (11 bytes, space-padded). */
static uchar
msdos_lfn_checksum(const uchar *shortname)
{
  uchar sum;
  int i;

  sum = 0;
  for(i = 11; i--; )
    sum = ((sum & 1) << 7) + (sum >> 1) + *shortname++;
  return sum;
}

/* Accumulate one LFN directory entry into a fat_lfn_state buffer.
 * LFN entries are stored in the directory in REVERSE sequence order
 * (highest sequence first), so each entry's characters go at position
 * (seq-1)*13 in our buffer regardless of read order.
 */
static void
msdos_accumulate_lfn(struct fat_lfn_state *st, struct fat_lfn_entry *le)
{
  int i;
  int seq;
  int base_pos;
  uchar pairs[26];

  if(st == 0 || le == 0)
    return;
  if(le->attr != MSDOS_ATTR_LFN)
    return;

  seq = le->ord & 0x1F;   /* 1-based sequence number */
  if(seq < 1 || seq > MSDOS_LFN_MAX_SEGS)
    return;

  if(le->ord & 0x40)      /* last LFN entry (highest seq) recorded first */
    st->chksum = le->chksum;

  /* Extract the 13 UTF-16LE characters from the three name fields */
  memmove(pairs,      le->name1, 10);
  memmove(pairs + 10, le->name2, 12);
  memmove(pairs + 22, le->name3,  4);

  base_pos = (seq - 1) * 13;
  for(i = 0; i < 13; i++){
    ushort c;
    int pos;

    c = (ushort)pairs[i * 2] | ((ushort)pairs[i * 2 + 1] << 8);
    if(c == 0x0000 || c == 0xFFFF)
      break;
    pos = base_pos + i;
    if(pos < MSDOS_LFN_MAX_CHARS){
      st->name[pos] = (c >= 0x0020 && c <= 0x007E) ? (char)c : '?';
      if(pos + 1 > st->len)
        st->len = pos + 1;
    }
  }
  st->name[st->len] = '\0';
  st->valid = 1;
}

/* Generate a basic 8.3 short name from a long name.
 * For names that already fit in 8.3, returns the converted form.
 * For longer names, produces a truncated name with ~1 appended.
 * Returns 0 on success, -1 if shortname cannot be derived.
 */
static int
msdos_generate_shortname(const char *name, uchar out[11])
{
  int i;
  int j;
  int k;
  int dot_pos;
  int name_len;
  char base[9];
  char ext[4];

  if(name == 0 || name[0] == 0)
    return -1;

  /* Try to fit as-is in 8.3 (reuse existing converter for compliant names) */
  if(msdos_component_to_83((char*)name, out) == 0)
    return 0;

  /* Build base and extension from the long name */
  name_len = strlen(name);
  dot_pos = -1;
  for(i = name_len - 1; i >= 0; i--){
    if(name[i] == '.'){
      dot_pos = i;
      break;
    }
  }

  memset(base, 0, sizeof(base));
  memset(ext, 0, sizeof(ext));
  j = 0;
  for(i = 0; i < (dot_pos >= 0 ? dot_pos : name_len) && j < 6; i++){
    char c = name[i];
    if(c == ' ' || c == '.' || c == '/' || c == '\\' || c == ':' ||
       c == '*' || c == '?' || c == '"' || c == '<' || c == '>' || c == '|')
      continue;
    if(c >= 'a' && c <= 'z')
      c -= ('a' - 'A');
    base[j++] = c;
  }
  if(j == 0)
    return -1;

  /* Append ~1 */
  base[j] = '~';
  base[j + 1] = '1';

  k = 0;
  if(dot_pos >= 0){
    for(i = dot_pos + 1; i < name_len && k < 3; i++){
      char c = name[i];
      if(c >= 'a' && c <= 'z')
        c -= ('a' - 'A');
      ext[k++] = c;
    }
  }

  for(i = 0; i < 11; i++)
    out[i] = ' ';
  for(i = 0; i < 8 && base[i]; i++)
    out[i] = (uchar)base[i];
  for(i = 0; i < 3 && ext[i]; i++)
    out[8 + i] = (uchar)ext[i];

  return 0;
}

static int
msdos_name_equals_ci(const char *a, const char *b)
{
  if(a == 0 || b == 0)
    return 0;

  while(*a && *b){
    char ca;
    char cb;

    ca = *a;
    cb = *b;
    if(ca >= 'A' && ca <= 'Z')
      ca += 'a' - 'A';
    if(cb >= 'A' && cb <= 'Z')
      cb += 'a' - 'A';
    if(ca != cb)
      return 0;
    a++;
    b++;
  }

  return *a == 0 && *b == 0;
}

static int
msdos_is_leap_year(uint year)
{
  if((year % 4U) != 0U)
    return 0;
  if((year % 100U) != 0U)
    return 1;
  return (year % 400U) == 0U;
}

static void
msdos_get_now_fat(ushort *date_out, ushort *time_out, uchar *tenth_out)
{
  struct rtcdate rtc;
  ushort fat_date;
  ushort fat_time;
  uchar fat_tenth;
  uint year;
  uint sec2;

  fat_date = 0;
  fat_time = 0;
  fat_tenth = 0;
  cmostime(&rtc);

  year = rtc.year;
  if(year < 1980U)
    year = 1980U;
  if(year > 2107U)
    year = 2107U;

  sec2 = rtc.second;
  if(sec2 > 59U)
    sec2 = 59U;

  fat_date = (ushort)(((year - 1980U) << 9) |
                      ((rtc.month & 0x0FU) << 5) |
                      (rtc.day & 0x1FU));
  fat_time = (ushort)(((rtc.hour & 0x1FU) << 11) |
                      ((rtc.minute & 0x3FU) << 5) |
                      ((sec2 / 2U) & 0x1FU));
  fat_tenth = (uchar)((sec2 & 1U) ? 100U : 0U);

  if(date_out)
    *date_out = fat_date;
  if(time_out)
    *time_out = fat_time;
  if(tenth_out)
    *tenth_out = fat_tenth;
}

static int
msdos_fat_datetime_to_epoch(ushort fat_date, ushort fat_time)
{
  static const uchar mdays[2][12] = {
    {31,28,31,30,31,30,31,31,30,31,30,31},
    {31,29,31,30,31,30,31,31,30,31,30,31},
  };
  uint year;
  uint month;
  uint day;
  uint hour;
  uint minute;
  uint second;
  uint64_t days;
  uint y;
  uint m;

  if(fat_date == 0)
    return 0;

  year = 1980U + ((uint)(fat_date >> 9) & 0x7FU);
  month = ((uint)(fat_date >> 5) & 0x0FU);
  day = (uint)(fat_date & 0x1FU);
  hour = ((uint)(fat_time >> 11) & 0x1FU);
  minute = ((uint)(fat_time >> 5) & 0x3FU);
  second = ((uint)(fat_time & 0x1FU)) * 2U;

  if(month < 1U || month > 12U)
    return 0;
  if(day < 1U || day > mdays[msdos_is_leap_year(year)][month - 1U])
    return 0;
  if(hour > 23U || minute > 59U || second > 59U)
    return 0;

  days = 0;
  for(y = 1970U; y < year; y++)
    days += msdos_is_leap_year(y) ? 366ULL : 365ULL;
  for(m = 1U; m < month; m++)
    days += (uint64_t)mdays[msdos_is_leap_year(year)][m - 1U];
  days += (uint64_t)(day - 1U);

  return (int)(days * 86400ULL +
               (uint64_t)hour * 3600ULL +
               (uint64_t)minute * 60ULL +
               (uint64_t)second);
}

static void
msdos_stamp_dirent(struct fat_dirent *de, int creating)
{
  ushort fat_date;
  ushort fat_time;
  uchar fat_tenth;

  if(de == 0)
    return;

  msdos_get_now_fat(&fat_date, &fat_time, &fat_tenth);
  if(creating){
    de->crt_date = fat_date;
    de->crt_time = fat_time;
    de->crt_tenth = fat_tenth;
  }
  de->acc_date = fat_date;
  de->wrt_date = fat_date;
  de->wrt_time = fat_time;
}

/* Write n_segs LFN directory entries starting at (start_sec, start_off) followed
 * by the 8.3 entry at (entry_sec, entry_off).  Entries are written in the
 * decreasing-sequence order required by the VFAT spec.
 */
static int
msdos_write_lfn_entries(struct msdos_mount_data *md,
                        uint start_sec, uint start_off,
                        const char *lfn_name, int n_segs,
                        const uchar shortname[11])
{
  uchar chksum;
  int   seg;
  uint  sec;
  uint  off;
  int   name_len;
  int   total_slots;

  if(md == 0 || lfn_name == 0 || n_segs < 1)
    return -1;

  chksum   = msdos_lfn_checksum(shortname);
  name_len = strlen(lfn_name);
  total_slots = n_segs + 1;  /* LFN entries + the 8.3 entry */

  sec = start_sec;
  off = start_off;

  /* Write LFN entries highest-seq first (decreasing order) */
  for(seg = n_segs; seg >= 1; seg--){
    struct fat_lfn_entry le;
    struct buf *b;
    int chars_start;
    int i;
    uchar pairs[26];

    memset(&le, 0, sizeof(le));
    le.ord    = (uchar)seg;
    if(seg == n_segs)
      le.ord |= 0x40;          /* mark as the last LFN entry */
    le.attr   = MSDOS_ATTR_LFN;
    le.type   = 0;
    le.chksum = chksum;
    le.fclus  = 0;

    chars_start = (seg - 1) * 13;
    memset(pairs, 0xFF, sizeof(pairs)); /* pad with 0xFFFF */

    for(i = 0; i < 13; i++){
      int pos = chars_start + i;
      ushort c;

      if(pos < name_len)
        c = (ushort)(uchar)lfn_name[pos];
      else if(pos == name_len)
        c = 0x0000;             /* NUL terminator */
      else
        c = 0xFFFF;             /* unused slots */

      pairs[i * 2]     = (uchar)(c & 0xFF);
      pairs[i * 2 + 1] = (uchar)(c >> 8);
    }

    memmove(le.name1, pairs,      10);
    memmove(le.name2, pairs + 10, 12);
    memmove(le.name3, pairs + 22,  4);

    b = msdos_bread(md, sec);
    if(b == 0)
      return -1;
    if(off + sizeof(le) > BSIZE){
      brelse(b);
      return -1;
    }
    memmove(b->data + off, &le, sizeof(le));
    bwrite(b);
    brelse(b);

    /* Advance to next slot */
    off += sizeof(struct fat_dirent);
    if(off >= BSIZE){
      off = 0;
      sec++;
    }
  }

  (void)total_slots;
  return 0;
}

/*
 * Find a run of n_needed consecutive free (0x00) or deleted (0xE5) directory
 * entries in the directory dp.  On success, sets first_sec+first_off to the
 * first entry of the run and last_inum to the inum of the LAST entry in the
 * run (which will become the 8.3 short-name slot).
 *
 * For the FAT16 fixed-root region this tries to grow the run across sectors.
 * For cluster-chain directories it may allocate a new cluster if needed.
 */
static int
msdos_find_free_run(struct inode *dp, int n_needed,
                    uint *first_sec, uint *first_off, uint *last_inum)
{
  struct msdos_mount_data *md;

  if(dp == 0 || first_sec == 0 || first_off == 0 || last_inum == 0 || n_needed < 1)
    return -1;
  if(dp->type != T_DIR)
    return -1;

  md = msdos_data_for_dev(dp->dev);
  if(md == 0)
    return -1;

  /* FAT16 fixed root directory */
  if(md->fat_type == 16 && dp->inum == ROOTINO && dp->addrs[2] == 1){
    uint slot;
    int run;
    uint run_start;

    run = 0;
    run_start = 0;
    for(slot = 0; slot < md->root_dir_entries; slot++){
      uint byte_off;
      uint sec;
      uint sec_off;
      struct buf *b;
      uchar first_byte;

      byte_off = slot * sizeof(struct fat_dirent);
      sec      = md->root_start + (byte_off / BSIZE);
      sec_off  = byte_off % BSIZE;

      b = msdos_bread(md, sec);
      if(b == 0)
        return -1;
      first_byte = b->data[sec_off];
      brelse(b);

      if(first_byte == 0x00 || first_byte == 0xE5){
        if(run == 0)
          run_start = slot;
        run++;
        if(run == n_needed){
          uint fs_byte;
          fs_byte     = run_start * sizeof(struct fat_dirent);
          *first_sec  = md->root_start + (fs_byte / BSIZE);
          *first_off  = fs_byte % BSIZE;
          byte_off    = slot * sizeof(struct fat_dirent);
          *last_inum  = MSDOS_ROOT16_INUM_BASE | slot;
          return 0;
        }
      } else {
        run = 0;
      }
    }
    return -1;
  }

  /* Cluster-chain directory (FAT16 subdir or FAT32 any dir) */
  if(dp->addrs[0] < 2)
    return -1;

  {
    uint cluster;
    uint last_cluster;
    int  run;
    uint run_start_sec;
    uint run_start_off;

    cluster      = dp->addrs[0];
    last_cluster = cluster;
    run          = 0;
    run_start_sec = 0;
    run_start_off = 0;

    while(cluster >= 2){
      uint csec;
      uint s;

      csec = msdos_cluster_first_sector(md, cluster);
      if(csec == 0)
        return -1;

      for(s = 0; s < md->sectors_per_cluster; s++){
        struct buf *b;
        uint entry_off;

        b = msdos_bread(md, csec + s);
        if(b == 0)
          return -1;
        for(entry_off = 0;
            entry_off + sizeof(struct fat_dirent) <= BSIZE;
            entry_off += sizeof(struct fat_dirent)){
          uchar fb;

          fb = b->data[entry_off];
          if(fb == 0x00 || fb == 0xE5){
            if(run == 0){
              run_start_sec = csec + s;
              run_start_off = entry_off;
            }
            run++;
            if(run == n_needed){
              uint slot_in_cluster;
              uint inum;

              brelse(b);
              *first_sec   = run_start_sec;
              *first_off   = run_start_off;
              slot_in_cluster =
                (s * BSIZE + entry_off) / sizeof(struct fat_dirent);
              inum = (cluster << MSDOS_CINUM_SLOT_BITS) |
                     (slot_in_cluster & MSDOS_CINUM_SLOT_MASK);
              if(inum == ROOTINO || (inum & MSDOS_ROOT16_INUM_BASE))
                inum ^= 0x1;        /* avoid collisions */
              *last_inum = inum;
              return 0;
            }
          } else {
            run = 0;
          }
        }
        brelse(b);
      }

      last_cluster = cluster;
      if(msdos_next_cluster(md, cluster, &cluster) < 0)
        return -1;
      if(cluster == 0)
        break;
    }

    /* Allocate a new cluster to extend the directory */
    {
      uint newc;
      uint inum;

      if(msdos_alloc_cluster(md, &newc) < 0)
        return -1;
      if(msdos_fat_set(md, last_cluster, newc) < 0)
        return -1;

      *first_sec  = msdos_cluster_first_sector(md, newc);
      *first_off  = 0;
      inum = (newc << MSDOS_CINUM_SLOT_BITS) |
             ((md->sectors_per_cluster * BSIZE / sizeof(struct fat_dirent) - 1)
              & MSDOS_CINUM_SLOT_MASK);
      if(inum == ROOTINO || (inum & MSDOS_ROOT16_INUM_BASE))
        inum ^= 0x1;
      *last_inum = inum;
      return 0;
    }
  }
}

/* Convenience wrapper: find a single free directory entry slot. */
static int
msdos_find_free_dirent(struct inode *dp, uint *sec, uint *off, uint *inum)
{
  return msdos_find_free_run(dp, 1, sec, off, inum);
}

static uint
msdos_eoc_value(struct msdos_mount_data *md)
{
  if(md->fat_type == 16)
    return 0xFFFF;
  return 0x0FFFFFFF;
}

static int
msdos_fat_get(struct msdos_mount_data *md, uint cluster, uint *out)
{
  uint off;
  uint sec;
  uint sec_off;
  struct buf *b;
  uint val;

  if(md == 0 || out == 0 || cluster < 2)
    return -1;

  if(md->fat_type == 16)
    off = cluster * 2;
  else
    off = cluster * 4;

  sec = md->fat_start + (off / BSIZE);
  sec_off = off % BSIZE;
  b = msdos_bread(md, sec);
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
  *out = val;
  return 0;
}

static int
msdos_fat_set(struct msdos_mount_data *md, uint cluster, uint value)
{
  uint off;
  uint sec_off;
  uint i;

  if(md == 0 || cluster < 2)
    return -1;

  if(md->fat_type == 16)
    off = cluster * 2;
  else
    off = cluster * 4;
  sec_off = off % BSIZE;

  for(i = 0; i < md->num_fats; i++){
    uint fat_base;
    uint sec;
    struct buf *b;

    fat_base = md->fat_start + i * md->sectors_per_fat;
    sec = fat_base + (off / BSIZE);
    b = msdos_bread(md, sec);
    if(b == 0)
      return -1;

    if(md->fat_type == 16){
      if(sec_off + 1 >= BSIZE){
        brelse(b);
        return -1;
      }
      b->data[sec_off] = value & 0xFF;
      b->data[sec_off + 1] = (value >> 8) & 0xFF;
    } else {
      uint old;
      uint nv;

      if(sec_off + 3 >= BSIZE){
        brelse(b);
        return -1;
      }
      old = (uint)b->data[sec_off] |
            ((uint)b->data[sec_off + 1] << 8) |
            ((uint)b->data[sec_off + 2] << 16) |
            ((uint)b->data[sec_off + 3] << 24);
      nv = (old & 0xF0000000U) | (value & 0x0FFFFFFFU);
      b->data[sec_off] = nv & 0xFF;
      b->data[sec_off + 1] = (nv >> 8) & 0xFF;
      b->data[sec_off + 2] = (nv >> 16) & 0xFF;
      b->data[sec_off + 3] = (nv >> 24) & 0xFF;
    }

    bwrite(b);
    brelse(b);
  }
  return 0;
}

static int
msdos_zero_cluster(struct msdos_mount_data *md, uint cluster)
{
  uint csec;
  uint s;

  if(md == 0 || cluster < 2)
    return -1;
  csec = msdos_cluster_first_sector(md, cluster);
  if(csec == 0)
    return -1;

  for(s = 0; s < md->sectors_per_cluster; s++){
    struct buf *b;

    b = msdos_bread(md, csec + s);
    if(b == 0)
      return -1;
    memset(b->data, 0, BSIZE);
    bwrite(b);
    brelse(b);
  }
  return 0;
}

static int
msdos_alloc_cluster(struct msdos_mount_data *md, uint *out)
{
  uint c;
  uint maxc;
  uint start;

  if(md == 0 || out == 0)
    return -1;

  /* Use free-cluster hint from FSInfo if plausible */
  start = 2;
  if(md->fat_type == 32 && md->free_cluster_count != 0xFFFFFFFF &&
     md->free_cluster_count == 0)
    return -1;  /* FSInfo says no free clusters */

  maxc = md->total_clusters + 1;
  for(c = start; c <= maxc; c++){
    uint v;

    if(msdos_fat_get(md, c, &v) < 0)
      return -1;
    if(v != 0)
      continue;

    if(msdos_fat_set(md, c, msdos_eoc_value(md)) < 0)
      return -1;
    if(msdos_zero_cluster(md, c) < 0)
      return -1;
    if(md->fat_type == 32 && md->free_cluster_count != 0xFFFFFFFF)
      md->free_cluster_count--;
    msdos_update_fsinfo(md);
    *out = c;
    return 0;
  }

  return -1;
}

static int
msdos_inode_dirent_location(struct msdos_mount_data *md, struct inode *ip,
                            uint *sec, uint *off)
{
  uint inum;

  if(md == 0 || ip == 0 || sec == 0 || off == 0)
    return -1;
  inum = ip->inum;
  if(inum == ROOTINO)
    return -1;

  /* FAT16 fixed-root slots: high bit set */
  if(inum & MSDOS_ROOT16_INUM_BASE){
    uint slot;
    uint byte_off;

    slot     = inum & ~MSDOS_ROOT16_INUM_BASE;
    byte_off = slot * sizeof(struct fat_dirent);
    *sec = md->root_start + (byte_off / BSIZE);
    *off = byte_off % BSIZE;
    return 0;
  }

  {
    uint dir_cluster;
    uint slot_in_cluster;
    uint byte_off;
    uint csec;

    dir_cluster     = inum >> MSDOS_CINUM_SLOT_BITS;
    slot_in_cluster = inum & MSDOS_CINUM_SLOT_MASK;
    if(dir_cluster < 2)
      return -1;

    byte_off = slot_in_cluster * sizeof(struct fat_dirent);
    csec = msdos_cluster_first_sector(md, dir_cluster);
    if(csec == 0)
      return -1;

    *sec = csec + (byte_off / BSIZE);
    *off = byte_off % BSIZE;
    return 0;
  }
}

static int
msdos_prev_dirent_location(struct inode *dp, uint sec, uint off,
                           uint *prev_sec, uint *prev_off)
{
  struct msdos_mount_data *md;

  if(dp == 0 || prev_sec == 0 || prev_off == 0)
    return -1;
  md = msdos_data_for_dev(dp->dev);
  if(md == 0)
    return -1;

  if(off >= sizeof(struct fat_dirent)){
    *prev_sec = sec;
    *prev_off = off - sizeof(struct fat_dirent);
    return 0;
  }

  if(md->fat_type == 16 && dp->inum == ROOTINO && dp->addrs[2] == 1){
    if(sec <= md->root_start)
      return -1;
    *prev_sec = sec - 1;
    *prev_off = BSIZE - sizeof(struct fat_dirent);
    return 0;
  }

  {
    uint cluster;
    uint prev_cluster;

    cluster = dp->addrs[0];
    prev_cluster = 0;
    while(cluster >= 2){
      uint csec;
      uint s;

      csec = msdos_cluster_first_sector(md, cluster);
      if(csec == 0)
        return -1;
      for(s = 0; s < md->sectors_per_cluster; s++){
        uint cur_sec;

        cur_sec = csec + s;
        if(cur_sec != sec)
          continue;
        if(s > 0){
          *prev_sec = cur_sec - 1;
          *prev_off = BSIZE - sizeof(struct fat_dirent);
          return 0;
        }
        if(prev_cluster < 2)
          return -1;
        *prev_sec = msdos_cluster_first_sector(md, prev_cluster) +
                    (md->sectors_per_cluster - 1);
        *prev_off = BSIZE - sizeof(struct fat_dirent);
        return 0;
      }
      prev_cluster = cluster;
      if(msdos_next_cluster(md, cluster, &cluster) < 0)
        return -1;
      if(cluster == 0)
        break;
    }
  }

  return -1;
}

static int
msdos_sync_inode_entry(struct inode *ip)
{
  struct msdos_mount_data *md;
  uint sec;
  uint off;
  struct buf *b;
  struct fat_dirent de;

  if(ip == 0)
    return -1;
  md = msdos_data_for_dev(ip->dev);
  if(md == 0)
    return -1;
  if(msdos_inode_dirent_location(md, ip, &sec, &off) < 0)
    return -1;

  b = msdos_bread(md, sec);
  if(b == 0)
    return -1;
  if(off + sizeof(de) > BSIZE){
    brelse(b);
    return -1;
  }

  memmove(&de, b->data + off, sizeof(de));
  de.size = ip->size;
  de.clu_lo = ip->addrs[0] & 0xFFFF;
  de.clu_hi = (ip->addrs[0] >> 16) & 0xFFFF;
  msdos_stamp_dirent(&de, 0);
  memmove(b->data + off, &de, sizeof(de));
  bwrite(b);
  brelse(b);
  return 0;
}

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
  if(cluster > md->total_clusters + 1)
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
  uint val;

  if(md == 0 || next == 0 || cluster < 2)
    return -1;
  if(msdos_fat_get(md, cluster, &val) < 0)
    return -1;

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

  // Handle FAT dot entries explicitly.
  if(name[0] == '.' && name[1] == 0){
    for(i = 0; i < 11; i++)
      out[i] = ' ';
    out[0] = '.';
    return 0;
  }
  if(name[0] == '.' && name[1] == '.' && name[2] == 0){
    for(i = 0; i < 11; i++)
      out[i] = ' ';
    out[0] = '.';
    out[1] = '.';
    return 0;
  }

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

      b = msdos_bread(md, csec + sec_idx);
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

  // FAT inodes are synthetic; do not call ilock() here because that
  // attempts xv6 dinode reads based on inum and can issue bogus block I/O.
  acquiresleep(&ip->lock);
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
  releasesleep(&ip->lock);
  return ip;
}

static int
msdos_dir_scan_fat16_root(struct msdos_mount_data *md,
                          int (*visit)(struct fat_dirent*, const char*, uint, uint, uint, int, uint, void*),
                          void *arg)
{
  uint slot;
  struct fat_lfn_state lfn;
  int lfn_slots;
  uint vis;

  memset(&lfn, 0, sizeof(lfn));
  lfn_slots = 0;
  vis = 0;

  for(slot = 0; slot < md->root_dir_entries; slot++){
    uint byte_off;
    uint sec;
    uint sec_off;
    struct buf *b;
    struct fat_dirent de;

    byte_off = slot * sizeof(struct fat_dirent);
    sec      = md->root_start + (byte_off / BSIZE);
    sec_off  = byte_off % BSIZE;

    b = msdos_bread(md, sec);
    if(b == 0)
      return -1;
    memmove(&de, b->data + sec_off, sizeof(de));
    brelse(b);

    if(de.name[0] == 0x00)
      return 0;   /* end of directory */

    if(de.name[0] == 0xE5){
      memset(&lfn, 0, sizeof(lfn));
      lfn_slots = 0;
      continue;
    }

    if(de.attr == MSDOS_ATTR_LFN){
      msdos_accumulate_lfn(&lfn, (struct fat_lfn_entry*)&de);
      lfn_slots++;
      continue;
    }

    if(de.attr & MSDOS_ATTR_VOLID){
      memset(&lfn, 0, sizeof(lfn));
      lfn_slots = 0;
      continue;
    }

    {
      const char *lfn_name = lfn.valid ? lfn.name : 0;
      uint inum = MSDOS_ROOT16_INUM_BASE | slot;
      int r = visit(&de, lfn_name, inum, sec, sec_off, lfn_slots, vis, arg);
      memset(&lfn, 0, sizeof(lfn));
      lfn_slots = 0;
      if(r != 0)
        return 1;
      vis++;
    }
  }

  return 0;
}

static int
msdos_dir_scan_cluster_chain(struct msdos_mount_data *md, uint first_cluster,
                             int (*visit)(struct fat_dirent*, const char*, uint, uint, uint, int, uint, void*),
                             void *arg)
{
  uint cluster;
  uint vis;
  struct fat_lfn_state lfn;
  int lfn_slots;

  if(first_cluster < 2)
    return 0;

  memset(&lfn, 0, sizeof(lfn));
  lfn_slots = 0;
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

      b = msdos_bread(md, csec + s);
      if(b == 0)
        return -1;

      for(off = 0; off + sizeof(struct fat_dirent) <= BSIZE; off += sizeof(struct fat_dirent)){
        struct fat_dirent de;
        uint slot_in_cluster;
        uint inum;

        memmove(&de, b->data + off, sizeof(de));

        if(de.name[0] == 0x00){
          brelse(b);
          return 0;  /* end of directory */
        }

        if(de.name[0] == 0xE5){
          memset(&lfn, 0, sizeof(lfn));
          lfn_slots = 0;
          continue;
        }

        if(de.attr == MSDOS_ATTR_LFN){
          msdos_accumulate_lfn(&lfn, (struct fat_lfn_entry*)&de);
          lfn_slots++;
          continue;
        }

        if(de.attr & MSDOS_ATTR_VOLID){
          memset(&lfn, 0, sizeof(lfn));
          lfn_slots = 0;
          continue;
        }

        slot_in_cluster = (s * BSIZE + off) / sizeof(struct fat_dirent);
        inum = (cluster << MSDOS_CINUM_SLOT_BITS) |
               (slot_in_cluster & MSDOS_CINUM_SLOT_MASK);
        if(inum == ROOTINO || (inum & MSDOS_ROOT16_INUM_BASE))
          inum ^= 0x1;

        {
          const char *lfn_name = lfn.valid ? lfn.name : 0;
          int r = visit(&de, lfn_name, inum, csec + s, off, lfn_slots, vis, arg);
          memset(&lfn, 0, sizeof(lfn));
          lfn_slots = 0;
          if(r != 0){
            brelse(b);
            return 1;
          }
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
               int (*visit)(struct fat_dirent*, const char*, uint, uint, uint, int, uint, void*),
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
  uint sec;
  uint off;
  int lfn_slots;
  int found;
};

static int
msdos_lookup_visit(struct fat_dirent *de, const char *lfn, uint inum,
                   uint sec, uint off, int lfn_slots, uint visidx, void *arg)
{
  struct lookup_ctx *ctx;
  int matched;

  (void)visidx;

  ctx = (struct lookup_ctx*)arg;
  matched = 0;

  /* Try 8.3 short name match first */
  if(msdos_name_matches_83(ctx->name, de))
    matched = 1;

  /* Try long filename match (case-insensitive ASCII) */
  if(!matched && lfn != 0 && msdos_name_equals_ci(ctx->name, lfn))
    matched = 1;

  if(!matched)
    return 0;

  memmove(&ctx->de, de, sizeof(*de));
  ctx->inum = inum;
  ctx->sec = sec;
  ctx->off = off;
  ctx->lfn_slots = lfn_slots;
  ctx->found = 1;
  return 1;
}

struct shortname_ctx {
  uchar shortname[11];
  int found;
};

static int
msdos_shortname_visit(struct fat_dirent *de, const char *lfn, uint inum,
                      uint sec, uint off, int lfn_slots, uint visidx, void *arg)
{
  struct shortname_ctx *ctx;

  (void)lfn;
  (void)inum;
  (void)sec;
  (void)off;
  (void)lfn_slots;
  (void)visidx;

  ctx = (struct shortname_ctx*)arg;
  if(memcmp(de->name, ctx->shortname, sizeof(ctx->shortname)) != 0)
    return 0;
  ctx->found = 1;
  return 1;
}

static int
msdos_shortname_exists(struct inode *dp, const uchar shortname[11])
{
  struct shortname_ctx ctx;

  memset(&ctx, 0, sizeof(ctx));
  memmove(ctx.shortname, shortname, sizeof(ctx.shortname));
  if(msdos_dir_scan(dp, msdos_shortname_visit, &ctx) < 0)
    return 0;
  return ctx.found;
}

struct locate_ctx {
  char *name;
  struct fat_dirent de;
  uint sec;
  uint off;
  int lfn_slots;
  int found;
};

static int
msdos_locate_visit(struct fat_dirent *de, const char *lfn, uint inum,
                   uint sec, uint off, int lfn_slots, uint visidx, void *arg)
{
  struct locate_ctx *ctx;

  (void)inum;
  (void)visidx;

  ctx = (struct locate_ctx*)arg;
  if(!msdos_name_matches_83(ctx->name, de) &&
     !(lfn != 0 && msdos_name_equals_ci(ctx->name, lfn)))
    return 0;

  memmove(&ctx->de, de, sizeof(ctx->de));
  ctx->sec = sec;
  ctx->off = off;
  ctx->lfn_slots = lfn_slots;
  ctx->found = 1;
  return 1;
}

static int
msdos_locate_entry(struct inode *dp, char *name, struct locate_ctx *ctx)
{
  if(dp == 0 || name == 0 || ctx == 0)
    return -1;
  memset(ctx, 0, sizeof(*ctx));
  ctx->name = name;
  if(msdos_dir_scan(dp, msdos_locate_visit, ctx) < 0)
    return -1;
  return ctx->found ? 0 : -1;
}

struct nth_ctx {
  uint want;
  uint cur;
  struct fat_dirent de;
  char lfn_name[MSDOS_LFN_MAX_CHARS + 1];
  uint inum;
  int found;
};

static int
msdos_nth_visit(struct fat_dirent *de, const char *lfn, uint inum,
                uint sec, uint off, int lfn_slots, uint visidx, void *arg)
{
  struct nth_ctx *ctx;

  (void)sec;
  (void)off;
  (void)lfn_slots;
  (void)visidx;

  ctx = (struct nth_ctx*)arg;
  if(ctx->cur == ctx->want){
    memmove(&ctx->de, de, sizeof(*de));
    if(lfn != 0)
      safestrcpy(ctx->lfn_name, lfn, sizeof(ctx->lfn_name));
    else
      ctx->lfn_name[0] = '\0';
    ctx->inum = inum;
    ctx->found = 1;
    return 1;
  }
  ctx->cur++;
  return 0;
}

static struct inode*
msdos_walk(struct vfs *fs, char *path, int nameiparent, char *name)
{
  struct inode *ip;
  struct msdos_mount_data *md;
  char elem[DIRSIZ + 1];
  int i;
  int e;

  if(path == 0)
    return 0;

  md = 0;
  if(fs)
    md = (struct msdos_mount_data*)fs->fs_data;
  if(md == 0)
    md = msdos_bootstrap_data;
  if(md == 0)
    md = msdos_data_for_dev(msdos_active_dev);
  if(md == 0)
    return 0;

  if(path[0] == '/'){
    ip = msdos_root_inode(fs);
    if(ip == 0)
      return 0;
  } else {
    ip = proc_cwd_idup();
    if(ip == 0 || msdos_data_for_dev(ip->dev) == 0){
      if(ip)
        iput(ip);
      ip = msdos_root_inode(fs);
      if(ip == 0)
        return 0;
    }
  }

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

/* Update the FAT32 FSInfo sector with the current free cluster count.
 * Silently no-ops on FAT16 or when no FSInfo sector is configured.
 */
static void
msdos_update_fsinfo(struct msdos_mount_data *md)
{
  struct fat_fsinfo *fi;
  struct buf *b;

  if(md == 0 || md->fat_type != 32 || md->fsinfo_sector == 0)
    return;

  b = msdos_bread(md, md->fsinfo_sector);
  if(b == 0)
    return;

  fi = (struct fat_fsinfo*)b->data;
  if(fi->lead_sig  != 0x41615252U ||
     fi->struc_sig != 0x61417272U ||
     fi->trail_sig != 0xAA550000U){
    brelse(b);
    return;
  }

  fi->free_count = md->free_cluster_count;
  fi->next_free  = 0xFFFFFFFF;   /* hint not tracked */
  bwrite(b);
  brelse(b);
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
  uint dev_blocks;

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
  dev_blocks = bdev_nblocks(m->dev);
  if(totsec > dev_blocks){
    // IDE probe can underestimate secondary raw-disk sizes; use BPB size
    // as a conservative override for this mounted block device.
    if(bdev_set_nblocks(m->dev, totsec) == 0)
      dev_blocks = bdev_nblocks(m->dev);
  }
  if(totsec > dev_blocks){
    cprintf("msdosfs: dev=%d BPB sectors=%d exceeds device blocks=%d\n",
            m->dev, totsec, dev_blocks);
    brelse(b);
    kfree((char*)md);
    return -1;
  }

  datasz = totsec - md->data_start;
  clusters = datasz / md->sectors_per_cluster;
  md->total_clusters = clusters;

  md->fsinfo_sector = 0;
  md->free_cluster_count = 0xFFFFFFFF;

  if(clusters < 65525){
    md->fat_type = 16;
    md->root_cluster = 0;
  } else {
    md->fat_type = 32;
    md->root_cluster = bpb32->root_clus;
    /* Load FSInfo free cluster count */
    if(bpb32->fs_info != 0 && bpb32->fs_info < totsec){
      struct fat_fsinfo *fi;
      struct buf *fib;

      md->fsinfo_sector = bpb32->fs_info;
      fib = msdos_bread(md, md->fsinfo_sector);
      if(fib != 0){
        fi = (struct fat_fsinfo*)fib->data;
        if(fi->lead_sig == 0x41615252U && fi->struc_sig == 0x61417272U &&
           fi->trail_sig == 0xAA550000U){
          md->free_cluster_count = fi->free_count;
        }
        brelse(fib);
      }
    }
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
msdos_root_inode(struct vfs *fs)
{
  struct msdos_mount_data *md;
  struct fat_dirent rootde;

  md = 0;
  if(fs)
    md = (struct msdos_mount_data*)fs->fs_data;
  if(md == 0)
    md = msdos_bootstrap_data;
  if(md == 0)
    md = msdos_data_for_dev(msdos_active_dev);
  if(md == 0)
    return 0;

  memset(&rootde, 0, sizeof(rootde));
  rootde.attr = MSDOS_ATTR_DIR;
  if(md->fat_type == 32){
    rootde.clu_lo = (ushort)(md->root_cluster & 0xFFFF);
    rootde.clu_hi = (ushort)((md->root_cluster >> 16) & 0xFFFF);
    return msdos_make_inode(md->dev, ROOTINO, &rootde, 0);
  }

  return msdos_make_inode(md->dev, ROOTINO, &rootde, 1);
}

static struct inode*
msdos_namei(struct vfs *fs, char *path)
{
  return msdos_walk(fs, path, 0, 0);
}

static struct inode*
msdos_nameiparent(struct vfs *fs, char *path, char *name)
{
  return msdos_walk(fs, path, 1, name);
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
  const char *nm;
  char nm83[16];
  uint dinum;
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
    dinum = ctx.inum & (uint)~MSDOS_ROOT16_INUM_BASE;
    dinum &= 0xFFFF;
    if(dinum == 0)
      dinum = 1;
    de.inum = (ushort)dinum;

    /* Prefer LFN name when available */
    if(ctx.lfn_name[0] != '\0'){
      nm = ctx.lfn_name;
    } else {
      memset(nm83, 0, sizeof(nm83));
      msdos_entry_name(&ctx.de, nm83, sizeof(nm83));
      nm = nm83;
    }
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
  uint old_first_cluster;
  uint cluster;
  uint first_cluster;
  uint last_cluster;
  uint cluster_count;
  uint need_clusters;
  uint new_end;
  uint cluster_bytes;
  uint skip;
  uint within;
  uint done;

  if(ip == 0 || src == 0)
    return -1;

  old_first_cluster = ip->addrs[0];

  md = msdos_data_for_dev(ip->dev);
  if(md == 0)
    return -1;
  if(md->sectors_per_cluster == 0)
    return -1;

  if(n == 0)
    return 0;
  // Disallow sparse writes; growth is supported via FAT allocation.
  if(off > ip->size)
    return -1;
  if(off + n < off)
    return -1;
  new_end = off + n;

  first_cluster = ip->addrs[0];

  cluster_bytes = md->sectors_per_cluster * BSIZE;
  if(cluster_bytes == 0)
    return -1;

  need_clusters = (new_end + cluster_bytes - 1) / cluster_bytes;
  if(need_clusters > 0){
    if(first_cluster < 2){
      if(msdos_alloc_cluster(md, &first_cluster) < 0)
        return -1;
      ip->addrs[0] = first_cluster;
    }

    cluster = first_cluster;
    last_cluster = cluster;
    cluster_count = 1;
    while(cluster_count < need_clusters){
      uint next;
      if(msdos_next_cluster(md, last_cluster, &next) < 0)
        return -1;
      if(next == 0)
        break;
      last_cluster = next;
      cluster_count++;
    }

    while(cluster_count < need_clusters){
      uint newc;

      if(msdos_alloc_cluster(md, &newc) < 0)
        return -1;
      if(msdos_fat_set(md, last_cluster, newc) < 0)
        return -1;
      last_cluster = newc;
      cluster_count++;
    }
  }

  cluster = ip->addrs[0];
  if(need_clusters > 0 && cluster < 2)
    return -1;

  skip  = off / cluster_bytes;
  within = off % cluster_bytes;
  done  = 0;                      /* initialize before the skip loop */

  while(skip > 0){
    if(msdos_next_cluster(md, cluster, &cluster) < 0)
      return -1;
    if(cluster == 0)
      return -1;
    skip--;
  }

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

      b = msdos_bread(md, csec + sec_idx);
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

  if(done > 0 && new_end > ip->size){
    ip->size = new_end;
    if(msdos_sync_inode_entry(ip) < 0)
      return -1;
  } else if(done > 0 && ip->addrs[0] != old_first_cluster){
    if(msdos_sync_inode_entry(ip) < 0)
      return -1;
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
  struct msdos_mount_data *md;

  if(ip == 0)
    return -1;
  if(ip->type != T_FILE)
    return -1;

  md = msdos_data_for_dev(ip->dev);
  if(md == 0)
    return -1;
  if(msdos_free_cluster_chain(md, ip->addrs[0]) < 0)
    return -1;

  ip->addrs[0] = 0;
  ip->size = 0;
  return msdos_sync_inode_entry(ip);
}

static int
msdos_stat(struct inode *ip, struct stat *st)
{
  struct msdos_mount_data *md;
  uint sec;
  uint off;
  struct buf *b;
  struct fat_dirent de;

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

  md = msdos_data_for_dev(ip->dev);
  if(md == 0)
    return 0;
  if(msdos_inode_dirent_location(md, ip, &sec, &off) < 0)
    return 0;
  b = msdos_bread(md, sec);
  if(b == 0)
    return 0;
  if(off + sizeof(de) > BSIZE){
    brelse(b);
    return 0;
  }
  memmove(&de, b->data + off, sizeof(de));
  brelse(b);

  st->st_atime = msdos_fat_datetime_to_epoch(de.acc_date, 0);
  st->st_mtime = msdos_fat_datetime_to_epoch(de.wrt_date, de.wrt_time);
  st->st_ctime = msdos_fat_datetime_to_epoch(de.crt_date, de.crt_time);
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

static struct inode*
msdos_create(struct inode *dp, char *name, short type, short major,
             short minor, int mode, int uid, int gid)
{
  struct msdos_mount_data *md;
  struct buf *b;
  struct fat_dirent de;
  uchar shortname[11];
  struct inode *existing;
  struct inode *ip;
  uint sec;
  uint off;
  uint inum;
  int n_segs;    /* number of LFN entries needed before the 8.3 slot */

  (void)major;
  (void)minor;
  (void)mode;
  (void)uid;
  (void)gid;

  if(dp == 0 || name == 0)
    return 0;
  if(dp->type != T_DIR)
    return 0;
  if(type != T_FILE && type != T_DIR)
    return 0;
  if(iaccess(dp, IACC_WRITE | IACC_EXEC) < 0)
    return 0;

  md = msdos_data_for_dev(dp->dev);
  if(md == 0)
    return 0;

  /* Generate 8.3 short name (or truncated ~1 form for long names) */
  if(msdos_generate_shortname(name, shortname) < 0)
    return 0;

  existing = msdos_dirlookup(dp, name, 0);
  if(existing != 0){
    iput(existing);
    return 0;
  }

  if(msdos_shortname_exists(dp, shortname))
    return 0;

  /* Determine whether LFN entries are needed */
  {
    uchar test83[11];
    n_segs = (msdos_component_to_83(name, test83) == 0) ? 0
             : (int)((strlen(name) + 12) / 13);
  }

  if(n_segs > 0){
    /* Locate a run of n_segs+1 consecutive free slots; write LFN entries
     * and record the sector/offset of the final 8.3 slot.
     */
    uint first_sec;
    uint first_off;
    uint last_inum;
    uint abs_lfn;
    uint abs_83;

    if(msdos_find_free_run(dp, n_segs + 1, &first_sec, &first_off, &last_inum) < 0)
      return 0;

    if(msdos_write_lfn_entries(md, first_sec, first_off, name, n_segs, shortname) < 0)
      return 0;

    /* The 8.3 entry sits immediately after the LFN entries */
    abs_lfn = first_sec * BSIZE + first_off;
    abs_83  = abs_lfn + (uint)n_segs * sizeof(struct fat_dirent);
    sec  = abs_83 / BSIZE;
    off  = abs_83 % BSIZE;
    inum = last_inum;
  } else {
    if(msdos_find_free_dirent(dp, &sec, &off, &inum) < 0)
      return 0;
  }

  /* Write the 8.3 directory entry */
  b = msdos_bread(md, sec);
  if(b == 0)
    return 0;
  if(off + sizeof(de) > BSIZE){
    brelse(b);
    return 0;
  }

  memset(&de, 0, sizeof(de));
  memmove(de.name, shortname, sizeof(de.name));
  msdos_stamp_dirent(&de, 1);

  if(type == T_DIR){
    uint dir_cluster;

    /* Allocate a cluster for the new directory */
    if(msdos_alloc_cluster(md, &dir_cluster) < 0){
      brelse(b);
      return 0;
    }

    de.attr   = MSDOS_ATTR_DIR;
    de.clu_lo = (ushort)(dir_cluster & 0xFFFF);
    de.clu_hi = (ushort)((dir_cluster >> 16) & 0xFFFF);
    de.size   = 0;

    memmove(b->data + off, &de, sizeof(de));
    bwrite(b);
    brelse(b);

    ip = msdos_make_inode(dp->dev, inum, &de, 0);
    if(ip == 0)
      return 0;
    ilock(ip);

    /* Write . and .. entries in the new directory cluster */
    {
      uint csec;
      struct buf *cb;
      struct fat_dirent dot;
      struct fat_dirent dotdot;
      uint parent_cluster;

      csec = msdos_cluster_first_sector(md, dir_cluster);
      cb = msdos_bread(md, csec);
      if(cb == 0){
        iunlockput(ip);
        return 0;
      }

      /* "." entry → points to the directory itself */
      memset(&dot, 0, sizeof(dot));
      memmove(dot.name, ".          ", 11);
      dot.attr   = MSDOS_ATTR_DIR;
      dot.clu_lo = de.clu_lo;
      dot.clu_hi = de.clu_hi;

      /* ".." entry → points to parent directory */
      parent_cluster = dp->addrs[0];
      /* For FAT16 root, parent cluster = 0 */
      if(md->fat_type == 16 && dp->inum == ROOTINO && dp->addrs[2] == 1)
        parent_cluster = 0;

      memset(&dotdot, 0, sizeof(dotdot));
      memmove(dotdot.name, "..         ", 11);
      dotdot.attr   = MSDOS_ATTR_DIR;
      dotdot.clu_lo = (ushort)(parent_cluster & 0xFFFF);
      dotdot.clu_hi = (ushort)((parent_cluster >> 16) & 0xFFFF);

      memmove(cb->data + 0,                    &dot,    sizeof(dot));
      memmove(cb->data + sizeof(struct fat_dirent), &dotdot, sizeof(dotdot));
      bwrite(cb);
      brelse(cb);
    }

    return ip;
  }

  /* Regular file */
  de.attr = MSDOS_ATTR_ARCHIVE;
  memmove(b->data + off, &de, sizeof(de));
  bwrite(b);
  brelse(b);

  ip = msdos_make_inode(dp->dev, inum, &de, 0);
  if(ip == 0)
    return 0;
  ilock(ip);
  return ip;
}

static int
msdos_delete_entry_chain(struct inode *dp, uint sec, uint off, int lfn_slots)
{
  struct msdos_mount_data *md;
  struct buf *b;
  int i;

  if(dp == 0)
    return -1;
  md = msdos_data_for_dev(dp->dev);
  if(md == 0)
    return -1;

  b = msdos_bread(md, sec);
  if(b == 0)
    return -1;
  if(off + sizeof(struct fat_dirent) > BSIZE){
    brelse(b);
    return -1;
  }
  memset(b->data + off, 0, sizeof(struct fat_dirent));
  b->data[off] = 0xE5;
  bwrite(b);
  brelse(b);

  for(i = 0; i < lfn_slots; i++){
    struct buf *lb;
    struct fat_dirent lde;

    if(msdos_prev_dirent_location(dp, sec, off, &sec, &off) < 0)
      break;
    lb = msdos_bread(md, sec);
    if(lb == 0)
      break;
    if(off + sizeof(lde) > BSIZE){
      brelse(lb);
      break;
    }
    memmove(&lde, lb->data + off, sizeof(lde));
    if(lde.attr != MSDOS_ATTR_LFN){
      brelse(lb);
      break;
    }
    memset(lb->data + off, 0, sizeof(lde));
    lb->data[off] = 0xE5;
    bwrite(lb);
    brelse(lb);
  }

  return 0;
}

static int
msdos_write_entry_chain(struct inode *dp, char *name, struct fat_dirent *srcde,
                        uint *out_inum)
{
  struct msdos_mount_data *md;
  struct fat_dirent de;
  struct buf *b;
  uchar shortname[11];
  uint sec;
  uint off;
  uint inum;
  int n_segs;

  if(dp == 0 || name == 0 || srcde == 0)
    return -1;
  md = msdos_data_for_dev(dp->dev);
  if(md == 0)
    return -1;
  if(msdos_generate_shortname(name, shortname) < 0)
    return -1;

  {
    uchar test83[11];
    n_segs = (msdos_component_to_83(name, test83) == 0) ? 0
             : (int)((strlen(name) + 12) / 13);
  }

  if(n_segs > 0){
    uint first_sec;
    uint first_off;
    uint last_inum;
    uint abs_lfn;
    uint abs_83;

    if(msdos_find_free_run(dp, n_segs + 1, &first_sec, &first_off, &last_inum) < 0)
      return -1;
    if(msdos_write_lfn_entries(md, first_sec, first_off, name, n_segs, shortname) < 0)
      return -1;

    abs_lfn = first_sec * BSIZE + first_off;
    abs_83 = abs_lfn + (uint)n_segs * sizeof(struct fat_dirent);
    sec = abs_83 / BSIZE;
    off = abs_83 % BSIZE;
    inum = last_inum;
  } else {
    if(msdos_find_free_dirent(dp, &sec, &off, &inum) < 0)
      return -1;
  }

  b = msdos_bread(md, sec);
  if(b == 0)
    return -1;
  if(off + sizeof(de) > BSIZE){
    brelse(b);
    return -1;
  }

  memmove(&de, srcde, sizeof(de));
  memmove(de.name, shortname, sizeof(de.name));
  msdos_stamp_dirent(&de, 0);
  memmove(b->data + off, &de, sizeof(de));
  bwrite(b);
  brelse(b);

  if(out_inum)
    *out_inum = inum;
  return 0;
}

static uint
msdos_parent_cluster(struct msdos_mount_data *md, struct inode *dp)
{
  if(md == 0 || dp == 0)
    return 0;
  if(md->fat_type == 16 && dp->inum == ROOTINO && dp->addrs[2] == 1)
    return 0;
  return dp->addrs[0];
}

static int
msdos_rewrite_dotdot(struct inode *ip, struct inode *newdp)
{
  struct msdos_mount_data *md;
  struct buf *b;
  struct fat_dirent de;
  uint parent_cluster;
  uint csec;

  if(ip == 0 || newdp == 0)
    return -1;
  md = msdos_data_for_dev(ip->dev);
  if(md == 0)
    return -1;
  if(ip->type != T_DIR || ip->addrs[0] < 2)
    return -1;

  csec = msdos_cluster_first_sector(md, ip->addrs[0]);
  if(csec == 0)
    return -1;
  b = msdos_bread(md, csec);
  if(b == 0)
    return -1;
  if(sizeof(struct fat_dirent) * 2 > BSIZE){
    brelse(b);
    return -1;
  }

  memmove(&de, b->data + sizeof(struct fat_dirent), sizeof(de));
  parent_cluster = msdos_parent_cluster(md, newdp);
  de.clu_lo = (ushort)(parent_cluster & 0xFFFF);
  de.clu_hi = (ushort)((parent_cluster >> 16) & 0xFFFF);
  msdos_stamp_dirent(&de, 0);
  memmove(b->data + sizeof(struct fat_dirent), &de, sizeof(de));
  bwrite(b);
  brelse(b);
  return 0;
}

static int
msdos_dir_is_ancestor(struct inode *ancestor, struct inode *dir)
{
  struct msdos_mount_data *md;
  uint ancestor_cluster;
  uint cur_cluster;
  int ancestor_is_root;
  int cur_is_root;

  if(ancestor == 0 || dir == 0)
    return 0;
  if(ancestor->dev != dir->dev || ancestor->type != T_DIR || dir->type != T_DIR)
    return 0;

  md = msdos_data_for_dev(ancestor->dev);
  if(md == 0)
    return 0;

  if(md->fat_type == 16){
    ancestor_is_root = ((ancestor->inum == ROOTINO && ancestor->addrs[2] == 1) ||
                        ancestor->addrs[0] == 0);
    cur_is_root = ((dir->inum == ROOTINO && dir->addrs[2] == 1) ||
                   dir->addrs[0] == 0);
  } else {
    ancestor_is_root = (ancestor->addrs[0] == md->root_cluster);
    cur_is_root = (dir->addrs[0] == md->root_cluster);
  }

  ancestor_cluster = ancestor->addrs[0];
  cur_cluster = dir->addrs[0];

  for(;;){
    if(ancestor_is_root){
      if(cur_is_root)
        return 1;
    } else if(!cur_is_root && cur_cluster == ancestor_cluster){
      return 1;
    }

    if(cur_is_root || cur_cluster < 2)
      return 0;

    {
      struct buf *b;
      struct fat_dirent de;
      uint csec;
      uint parent_cluster;
      int parent_is_root;

      csec = msdos_cluster_first_sector(md, cur_cluster);
      if(csec == 0)
        return 0;
      b = msdos_bread(md, csec);
      if(b == 0)
        return 0;
      if(sizeof(struct fat_dirent) * 2 > BSIZE){
        brelse(b);
        return 0;
      }

      memmove(&de, b->data + sizeof(struct fat_dirent), sizeof(de));
      brelse(b);

      parent_cluster = msdos_entry_cluster(&de);
      if(md->fat_type == 16)
        parent_is_root = (parent_cluster == 0);
      else
        parent_is_root = (parent_cluster == md->root_cluster);

      if(parent_is_root == cur_is_root && parent_cluster == cur_cluster)
        return 0;

      cur_cluster = parent_cluster;
      cur_is_root = parent_is_root;
    }
  }
}

static int
msdos_remove(struct inode *dp, char *name)
{
  struct msdos_mount_data *md;
  struct inode *ip;
  struct locate_ctx loc;
  uint sec;
  uint off;

  if(dp == 0 || name == 0)
    return -1;
  if(dp->type != T_DIR)
    return -1;

  md = msdos_data_for_dev(dp->dev);
  if(md == 0)
    return -1;

  if(msdos_locate_entry(dp, name, &loc) < 0)
    return -1;

  ip = msdos_dirlookup(dp, name, 0);
  if(ip == 0)
    return -1;
  ilock(ip);

  /* For directories, verify the dir is empty (only . and ..) */
  if(ip->type == T_DIR){
    struct nth_ctx ctx;
    int i;
    int has_other;

    has_other = 0;
    for(i = 0; i < 64; i++){
      char nm83[12];
      memset(&ctx, 0, sizeof(ctx));
      ctx.want = (uint)i;
      if(msdos_dir_scan(ip, msdos_nth_visit, &ctx) < 0)
        break;
      if(!ctx.found)
        break;
      msdos_entry_name(&ctx.de, nm83, sizeof(nm83));
      if(nm83[0] == '.' && (nm83[1] == '\0' ||
         (nm83[1] == '.' && nm83[2] == '\0')))
        continue;
      has_other = 1;
      break;
    }
    if(has_other){
      iunlockput(ip);
      return -1;   /* directory not empty */
    }
  } else if(ip->type != T_FILE){
    iunlockput(ip);
    return -1;
  }

  if(msdos_free_cluster_chain(md, ip->addrs[0]) < 0){
    iunlockput(ip);
    return -1;
  }
  sec = loc.sec;
  off = loc.off;
  if(msdos_delete_entry_chain(dp, sec, off, loc.lfn_slots) < 0){
    iunlockput(ip);
    return -1;
  }

  ip->addrs[0] = 0;
  ip->size = 0;
  ip->nlink = 0;
  ip->valid = 0;
  iunlockput(ip);
  return 0;
}

static int
msdos_rename(struct inode *olddp, char *oldname,
             struct inode *newdp, char *newname)
{
  struct locate_ctx oldloc;
  struct locate_ctx newloc;
  struct inode *ip;
  struct inode *exist;
  uchar shortname[11];
  uint new_inum;
  int is_dir;

  if(olddp == 0 || newdp == 0 || oldname == 0 || newname == 0)
    return -1;
  if(olddp->dev != newdp->dev)
    return -1;
  if(olddp->type != T_DIR || newdp->type != T_DIR)
    return -1;
  if(msdos_locate_entry(olddp, oldname, &oldloc) < 0)
    return -1;
  if(msdos_generate_shortname(newname, shortname) < 0)
    return -1;

  ip = msdos_dirlookup(olddp, oldname, 0);
  if(ip == 0)
    return -1;
  ilock(ip);
  is_dir = (ip->type == T_DIR);

  if(is_dir && msdos_dir_is_ancestor(ip, newdp)){
    iunlockput(ip);
    return -1;
  }

  if(msdos_locate_entry(newdp, newname, &newloc) == 0){
    if(olddp == newdp && oldloc.sec == newloc.sec && oldloc.off == newloc.off){
      iunlockput(ip);
      return 0;
    }

    exist = msdos_dirlookup(newdp, newname, 0);
    if(exist == 0){
      iunlockput(ip);
      return -1;
    }
    ilock(exist);
    if(exist->type == T_DIR || is_dir){
      iunlockput(exist);
      iunlockput(ip);
      return -1;
    }
    iunlock(exist);
    iput(exist);

    if(msdos_remove(newdp, newname) < 0){
      iunlockput(ip);
      return -1;
    }
  }

  if(msdos_shortname_exists(newdp, shortname) &&
     !(olddp == newdp && memcmp(oldloc.de.name, shortname, sizeof(shortname)) == 0)){
    iunlockput(ip);
    return -1;
  }

  if(msdos_write_entry_chain(newdp, newname, &oldloc.de, &new_inum) < 0){
    iunlockput(ip);
    return -1;
  }

  if(is_dir && olddp != newdp){
    if(msdos_rewrite_dotdot(ip, newdp) < 0){
      if(msdos_locate_entry(newdp, newname, &newloc) == 0)
        msdos_delete_entry_chain(newdp, newloc.sec, newloc.off, newloc.lfn_slots);
      iunlockput(ip);
      return -1;
    }
  }

  if(msdos_delete_entry_chain(olddp, oldloc.sec, oldloc.off, oldloc.lfn_slots) < 0){
    iunlockput(ip);
    return -1;
  }

  (void)new_inum;
  iunlockput(ip);
  return 0;
}

void
vfs_msdosfs_init(struct vfs *fs)
{
  if(fs == 0)
    return;

  safestrcpy(fs->name, "msdosfs", sizeof(fs->name));
  fs->caps = VFS_CAP_READ | VFS_CAP_WRITE | VFS_CAP_CREATE | VFS_CAP_REMOVE |
             VFS_CAP_MKDIR | VFS_CAP_RENAME;
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
  fs->vnode_ops.remove = msdos_remove;
  fs->vnode_ops.rename = msdos_rename;
  fs->vnode_ops.create = msdos_create;
}
