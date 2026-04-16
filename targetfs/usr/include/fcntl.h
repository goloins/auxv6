#ifndef _FCNTL_H_
#define _FCNTL_H_

#include "sys/types.h"

#define O_RDONLY  0x000
#define O_WRONLY  0x001
#define O_RDWR    0x002
#define O_CREATE  0x200
#define O_APPEND  0x400
#define O_TRUNC   0x800

// Additional O_* flags for compatibility
#define O_CREAT   O_CREATE
#define O_EXCL    0x1000
#define O_NONBLOCK 0x2000
#define O_NOCTTY  0x4000
#define O_CLOEXEC 0x8000

#ifndef AT_FDCWD
#define AT_FDCWD (-100)
#endif

#ifndef AT_SYMLINK_NOFOLLOW
#define AT_SYMLINK_NOFOLLOW 0x100
#endif

#ifndef AT_EACCESS
#define AT_EACCESS 0x200
#endif

// fcntl() commands
#define F_DUPFD   0   // Duplicate file descriptor
#define F_GETFD   1   // Get file descriptor flags
#define F_SETFD   2   // Set file descriptor flags
#define F_GETFL   3   // Get file status flags
#define F_SETFL   4   // Set file status flags
#define F_GETLK   5   // Get record locking info
#define F_SETLK   6   // Set record locking info
#define F_SETLKW  7   // Set record locking info; wait if blocked
#define F_DUPFD_CLOEXEC 1030  // Duplicate fd with close-on-exec

// Record lock types (for struct flock / fcntl locks)
#define F_RDLCK   0
#define F_WRLCK   1
#define F_UNLCK   2

struct flock {
	short l_type;    // F_RDLCK, F_WRLCK, or F_UNLCK
	short l_whence;  // SEEK_SET, SEEK_CUR, SEEK_END
	off_t l_start;   // Relative starting offset
	off_t l_len;     // Number of bytes; 0 means to EOF
	int   l_pid;     // PID of lock owner (for F_GETLK)
};

// File descriptor flags (for F_GETFD/F_SETFD)
#define FD_CLOEXEC 1  // Close-on-exec flag

// lseek() whence values
#define SEEK_SET  0   // Set file offset to offset
#define SEEK_CUR  1   // Set file offset to current + offset
#define SEEK_END  2   // Set file offset to EOF + offset

#define MNT_RDONLY 0x0001
#define MNT_NOSUID 0x0002
#define MNT_NODEV  0x0004
#define MNT_NOEXEC 0x0008
#define MNT_SYNC   0x0010
#define MNT_REMOUNT 0x0020

#define MNT_DEVSHIFT 16
#define MNT_DEVMASK  0x00ff0000
// Encode dev override as (dev + 1) so dev=0 is representable.
#define MNT_MAKEDEV(d) ((((d) + 1) & 0xff) << MNT_DEVSHIFT)
#define MNT_HASDEV(f) (((f) & MNT_DEVMASK) != 0)
#define MNT_GETDEV(f) ((((f) & MNT_DEVMASK) >> MNT_DEVSHIFT) - 1)

#define HD_DISK_UNITS 4
#define HD_PARTS_PER_DISK 4
#define HD_DISK_DEV(unit) (unit)
#define HD_PART_BASE HD_DISK_UNITS
#define HD_PART_DEV(unit, partno) (HD_PART_BASE + (unit) * HD_PARTS_PER_DISK + ((partno) - 1))

#define HD_DEVICE_COUNT (HD_DISK_UNITS + HD_DISK_UNITS * HD_PARTS_PER_DISK)

#define VD_DISK_UNITS 4
#define VD_PARTS_PER_DISK 4
#define VD_DISK_BASE HD_DEVICE_COUNT
#define VD_DISK_DEV(unit) (VD_DISK_BASE + (unit))
#define VD_PART_BASE (VD_DISK_BASE + VD_DISK_UNITS)
#define VD_PART_DEV(unit, partno) (VD_PART_BASE + (unit) * VD_PARTS_PER_DISK + ((partno) - 1))

#define ND_DISK_UNITS 4
#define ND_DISK_BASE (VD_PART_BASE + VD_DISK_UNITS * VD_PARTS_PER_DISK)
#define ND_DISK_DEV(unit) (ND_DISK_BASE + (unit))

int open(const char *path, int oflag, ...);
int creat(const char *path, int mode);
int fcntl(int fd, int cmd, ...);

#endif /* _FCNTL_H_ */
