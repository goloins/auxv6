//
// ext2 filesystem support for VFS
//

#include "types.h"
#include "defs.h"
#include "param.h"
#include "spinlock.h"
#include "sleeplock.h"
#include "fs.h"
#include "vfs.h"
#include "file.h"
#include "fcntl.h"

// ext2 Superblock structure (1024 bytes, located at byte 1024)
struct ext2_superblock {
  uint s_inodes_count;      // Total inode count
  uint s_blocks_count;      // Total block count
  uint s_r_blocks_count;    // Reserved block count
  uint s_free_blocks_count; // Free block count
  uint s_free_inodes_count; // Free inode count
  uint s_first_data_block;  // First Data Block (usually 1)
  uint s_log_block_size;    // Block size = 1024 << s_log_block_size
  uint s_log_frag_size;     // Fragment size = 1024 << s_log_frag_size
  uint s_blocks_per_group;   // Blocks per group
  uint s_frags_per_group;    // Fragments per group
  uint s_inodes_per_group;   // Inodes per group
  uint s_mtime;             // Mounting time
  uint s_wtime;             // Last write time
  ushort s_mnt_count;       // Mount count
  short s_max_mnt_count;    // Max mount count
  ushort s_magic;           // 0xEF53 - ext2 signature
  ushort s_state;           // Filesystem state
  ushort s_errors;          // Error handling
  ushort s_minor_rev_level; // Minor revision level
  uint s_lastcheck;         // Last check time
  uint s_checkinterval;     // Max time between checks
  uint s_creator_os;        // Creator OS
  uint s_rev_level;         // Revision level
  ushort s_def_resuid;      // Default reserved UID
  ushort s_def_resgid;      // Default reserved GID
  // Extended superblock follows in rev_level >= 1
};

#define EXT2_SUPER_MAGIC 0xEF53
#define EXT2_SB_SIZE 1024  // Superblock size

// ext2 Block Group Descriptor
struct ext2_group_desc {
  uint bg_block_bitmap;     // Block bitmap block
  uint bg_inode_bitmap;     // Inode bitmap block
  uint bg_inode_table;      // Inode table start block
  ushort bg_free_blocks_count;
  ushort bg_free_inodes_count;
  ushort bg_used_dirs_count;
  ushort bg_pad;
  uint bg_reserved[3];
};

#define EXT2_BLOCK_GROUP_DESC_SIZE 32

// ext2 Inode structure (on-disk)
struct ext2_inode {
  ushort i_mode;            // Type and permissions
  ushort i_uid;             // Owner UID
  uint i_size;              // File size in bytes
  uint i_atime;             // Access time
  uint i_ctime;             // Creation time
  uint i_mtime;             // Modification time
  uint i_dtime;             // Deletion time
  ushort i_gid;             // Owner GID
  ushort i_links_count;     // Link count
  uint i_blocks;            // Number of 512-byte blocks
  uint i_flags;             // File flags
  uint i_osd1;              // OS-specific value
  uint i_block[15];         // Block pointers (12 direct, 3 indirect)
  uint i_generation;        // Generation number
  uint i_file_acl;          // File ACL location
  uint i_dir_acl;           // Directory ACL location
  uint i_faddr;             // Fragment address
  uint i_osd2[3];           // OS-specific value
};

#define EXT2_INODE_SIZE 128

// ext2 Directory entry
struct ext2_dirent {
  uint inode;               // Inode number
  ushort rec_len;           // Record length (must be a multiple of 4)
  uchar name_len;           // Name length (without null terminator)
  uchar file_type;          // File type indicator
  char name[0];             // Name (variable length, followed by padding)
};

#define EXT2_FT_UNKNOWN 0
#define EXT2_FT_REG_FILE 1
#define EXT2_FT_DIR 2
#define EXT2_FT_CHRDEV 3
#define EXT2_FT_BLKDEV 4
#define EXT2_FT_FIFO 5
#define EXT2_FT_SOCK 6
#define EXT2_FT_SYMLINK 7

// ext2 filesystem mountpoint data
struct ext2_mount_data {
  struct ext2_superblock sb;
  struct ext2_group_desc *group_descs;
  uint group_count;
  uint block_size;
  uint inode_size;
  uint block_shift;
  int dev;
};

// VFS operations for ext2 (all stubbed for now)

static int
ext2_read(struct inode *ip, char *dst, uint off, uint n)
{
  return -1;  // Not yet implemented
}

static int
ext2_write(struct inode *ip, char *src, uint off, uint n)
{
  return -1;  // Not yet implemented
}

static int
ext2_stat(struct inode *ip, struct stat *st)
{
  return -1;  // Not yet implemented
}

static int
ext2_access(struct inode *ip, int mode)
{
  return -1;  // Not yet implemented
}

static struct inode*
ext2_dirlookup(struct inode *dp, char *name, uint *poff)
{
  return 0;  // Not yet implemented
}

static int
ext2_dirlink(struct inode *dp, char *name, uint inum)
{
  return -1;  // Not yet implemented
}

// VFS layer operations for ext2

static struct inode*
ext2_namei(char *path)
{
  return 0;  // Not yet implemented
}

static struct inode*
ext2_nameiparent(char *path, char *name)
{
  return 0;  // Not yet implemented
}

static void
ext2_inode_put(struct inode *ip)
{
  // Release reference to ext2 inode
  iput(ip);
}

// ext2 mount initialization
// This callback is invoked during vfs_register_mount to allow ext2 to
// read and cache the superblock and group descriptor table.
static int
ext2_mount_init(struct mount *m)
{
  struct ext2_mount_data *data;
  
  // Allocate per-mount data structure
  data = (struct ext2_mount_data *)kalloc();
  if(data == 0)
    return -1;
  
  // Store device number
  data->dev = m->dev;
  
  // TODO: Read superblock from device at byte offset 1024
  // TODO: Verify magic number (0xEF53)
  // TODO: Calculate block size and inode size
  // TODO: Read and cache group descriptor table
  
  m->fs_data = (void *)data;
  return 0;
}

void
ext2_mount_destroy(struct vfs *fs)
{
  // TODO: Free cached group descriptors and superblock
  // Note: This is called when unmounting, fs might contain
  // references to multiple mounts via their fs_data pointers.
  // For now, we only clean up during unmount (mount->fs_data reset).
}

void
vfs_ext2_init(struct vfs *fs)
{
  if(fs == 0)
    return;

  safestrcpy(fs->name, "ext2", sizeof(fs->name));
  // ext2 supports full feature set (once implemented)
  fs->caps = VFS_CAP_READ | VFS_CAP_WRITE | VFS_CAP_CREATE |
             VFS_CAP_REMOVE | VFS_CAP_LINK | VFS_CAP_MKDIR;
  fs->fs_data = 0;
  fs->fs_destroy = ext2_mount_destroy;
  fs->mount_init = ext2_mount_init;
  fs->ops.namei = ext2_namei;
  fs->ops.nameiparent = ext2_nameiparent;
  fs->ops.inode_put = ext2_inode_put;
  fs->vnode_ops.read = ext2_read;
  fs->vnode_ops.write = ext2_write;
  fs->vnode_ops.stat = ext2_stat;
  fs->vnode_ops.access = ext2_access;
  fs->vnode_ops.dirlookup = ext2_dirlookup;
  fs->vnode_ops.dirlink = ext2_dirlink;
}
