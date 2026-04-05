#include "stdint.h"

#define T_DIR     1   // Directory
#define T_FILE    2   // File
#define T_DEV     3   // Device
#define T_SYMLINK 4   // Symbolic link

#define M_IFMT   0170000
#define M_IFREG  0100000
#define M_IFDIR  0040000
#define M_IFCHR  0020000
#define M_IFBLK  0060000
#define M_IFLNK  0120000   // Symbolic link

#define M_ISUID 04000
#define M_ISGID 02000
#define M_ISVTX 01000
#define M_IRUSR 00400
#define M_IWUSR 00200
#define M_IXUSR 00100
#define M_IRGRP 00040
#define M_IWGRP 00020
#define M_IXGRP 00010
#define M_IROTH 00004
#define M_IWOTH 00002
#define M_IXOTH 00001

struct stat {
  short st_type;   // Type of file (auxv6: T_FILE, T_DIR, T_DEV)
  int st_dev;      // File system's disk device
  uint st_ino;     // Inode number
  short st_major;  // Major device number (T_DEV)
  short st_minor;  // Minor device number (T_DEV)
  short st_nlink;  // Number of links to file
  short st_uid;    // Owner user ID
  short st_gid;    // Owner group ID
  ushort st_mode;  // File type and permission bits
  uint64_t st_size; // Size of file in bytes (64-bit; supports files up to 2^64-1 bytes)
  int st_atime;    // Time of last access (seconds since epoch; 0 if not tracked)
  int st_mtime;    // Time of last modification (seconds since epoch; 0 if not tracked)
  int st_ctime;    // Time of status change (seconds since epoch; 0 if not tracked)
};
