/* posix/fcntl.h — POSIX compat shim for auxv6 */
#ifndef _POSIX_FCNTL_H
#define _POSIX_FCNTL_H

#include_next <fcntl.h>

#ifndef AT_FDCWD
#define AT_FDCWD (-100)
#endif

#ifndef AT_EACCESS
#define AT_EACCESS 0x200
#endif

#ifndef AT_SYMLINK_NOFOLLOW
#define AT_SYMLINK_NOFOLLOW 0x100
#endif

/* Large-file aliases — auxv6 has no 64-bit file distinction */
#define open64   open
#define creat64  creat

#endif /* _POSIX_FCNTL_H */
