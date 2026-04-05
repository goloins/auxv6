struct file {
  enum { FD_NONE, FD_PIPE, FD_INODE, FD_SOCKET } type;
  int ref; // reference count
  char readable;
  char writable;
  char pty_side; /* 0=none, 1=master, 2=slave */
  short pty_index;
  struct pipe *pipe;
  struct inode *ip;
  struct socket *socket;
  uint off;
};


// in-memory copy of an inode
struct inode {
  uint dev;           // Device number
  uint inum;          // Inode number
  int ref;            // Reference count
  struct sleeplock lock; // protects everything below here
  int valid;          // inode has been read from disk?

  struct inode *hash_next; // next in icache hash chain

  short type;         // copy of disk inode
  short major;
  short minor;
  short nlink;
  short uid;
  short gid;
  short mode;
  uint size;
  uint addrs[NADDRS];
};

// table mapping major device number to
// device functions
struct devsw {
  int (*read)(struct inode*, char*, uint, int);
  int (*write)(struct inode*, char*, uint, int);
};

extern struct devsw devsw[];

#define CONSOLE 1
#define BLOCKDEV 2
#define PTYDEV 3
#define PTY_MAX_UNITS 16
#define PTY_MINOR_PTMX 0
#define PTY_MINOR_SLAVE_BASE 1
#define PTY_SIDE_NONE 0
#define PTY_SIDE_MASTER 1
#define PTY_SIDE_SLAVE 2
#define DISK_MAX_UNITS 4
#define DISK_PARTS_PER_DISK 4
#define DISK_DEV(unit) (unit)
#define DISK_PART_BASE DISK_MAX_UNITS
#define DISK_PART_DEV(unit, partno) (DISK_PART_BASE + (unit) * DISK_PARTS_PER_DISK + ((partno) - 1))

#define PROCFSDEV 31
#define EXT2DEV DISK_DEV(2)
#define NFSDEV_BASE 44
#define NFSDEV_MAX 4
#define TMPFSDEV_BASE 48
#define TMPFSDEV_MAX 8

#include "rootfs_config.h"
