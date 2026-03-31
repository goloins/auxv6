/* posix/fcntl.h — POSIX compat shim for auxv6 */
#ifndef _POSIX_FCNTL_H
#define _POSIX_FCNTL_H

#include_next <fcntl.h>

/* Large-file aliases — auxv6 has no 64-bit file distinction */
#define open64   open
#define creat64  creat
#define lseek64  lseek

#endif /* _POSIX_FCNTL_H */
