/*
 * <sys/stat.h> - POSIX file status
 *
 * Wraps auxv6's stat.h and provides standard POSIX S_* names in addition
 * to the auxv6 M_* names.  Ported software should include this header
 * rather than "stat.h" directly.
 */

#ifndef _SYS_STAT_H
#define _SYS_STAT_H

/* Chain to include/sys/stat.h (which wraps include/stat.h) */
#include_next <sys/stat.h>
/* Pull in mode_t and friends */
#include <sys/types.h>

/* POSIX S_IF* aliases for auxv6 M_IF* constants */
#define S_IFMT   M_IFMT
#define S_IFREG  M_IFREG
#define S_IFDIR  M_IFDIR
#define S_IFCHR  M_IFCHR
#define S_IFBLK  M_IFBLK
#define S_IFIFO  0010000
#define S_IFLNK  0120000
#define S_IFSOCK 0140000

/* POSIX permission bit aliases */
#define S_ISUID  M_ISUID
#define S_ISGID  M_ISGID
#define S_ISVTX  M_ISVTX

#define S_IRUSR  M_IRUSR
#define S_IWUSR  M_IWUSR
#define S_IXUSR  M_IXUSR
#define S_IRGRP  M_IRGRP
#define S_IWGRP  M_IWGRP
#define S_IXGRP  M_IXGRP
#define S_IROTH  M_IROTH
#define S_IWOTH  M_IWOTH
#define S_IXOTH  M_IXOTH

#define S_IRWXU  (S_IRUSR|S_IWUSR|S_IXUSR)
#define S_IRWXG  (S_IRGRP|S_IWGRP|S_IXGRP)
#define S_IRWXO  (S_IROTH|S_IWOTH|S_IXOTH)

/* POSIX file type test macros */
#define S_ISREG(m)  (((m) & S_IFMT) == S_IFREG)
#define S_ISDIR(m)  (((m) & S_IFMT) == S_IFDIR)
#define S_ISCHR(m)  (((m) & S_IFMT) == S_IFCHR)
#define S_ISBLK(m)  (((m) & S_IFMT) == S_IFBLK)
#define S_ISFIFO(m) (((m) & S_IFMT) == S_IFIFO)
#define S_ISLNK(m)  (((m) & S_IFMT) == S_IFLNK)
#define S_ISSOCK(m) (((m) & S_IFMT) == S_IFSOCK)

/*
 * stat64/fstat64 - auxv6 has no large-file distinction; alias to the
 * standard calls.  Ported Linux software uses these names.
 */
#define stat64   stat
#define fstat64  fstat
#define lstat64  lstat

/* lstat: auxv6 has no symlinks; fall back to stat */
#define lstat(path, buf) stat((path), (buf))

/* Function declarations */
int stat(const char *path, struct stat *buf);
int fstat(int fd, struct stat *buf);

/* umask - not yet a real syscall; stub returns 0 */
#ifndef _UMASK_DECLARED
#define _UMASK_DECLARED
static inline mode_t umask(mode_t mask) { (void)mask; return 022; }
#endif

#endif /* _SYS_STAT_H */
