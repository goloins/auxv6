#define T_DIR  1   // Directory
#define T_FILE 2   // File
#define T_DEV  3   // Device

#define M_IFMT  0170000
#define M_IFREG 0100000
#define M_IFDIR 0040000
#define M_IFCHR 0020000
#define M_IFBLK 0060000

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
  short type;  // Type of file
  int dev;     // File system's disk device
  uint ino;    // Inode number
  short major; // Major device number (T_DEV)
  short minor; // Minor device number (T_DEV)
  short nlink; // Number of links to file
  short uid;   // Owner user ID
  short gid;   // Owner group ID
  ushort mode; // Permission bits
  uint size;   // Size of file in bytes
};
