/*
 * <unistd.h> - Standard Symbolic Constants and Types
 *
 * POSIX.1-2017 compatible definitions
 *
 * This header provides:
 * - POSIX version macros
 * - Standard file descriptor numbers
 * - Standard symbolic constants
 * - Function declarations
 *
 * Tranche 1 note:
 * - configuration, login, PTY, and path/tempfile portability helpers are
 *   being filled in incrementally after the ABI cleanup.
 */

#ifndef _UNISTD_H
#define _UNISTD_H

#include "sys/types.h"
#include "sys/select.h"
#include "poll.h"
#include "stddef.h"
#include "termios.h"
#include "sys/ioctl.h"

/* POSIX version identification */
#define _POSIX_VERSION          200809L
#define _POSIX2_VERSION         200809L
#define _XOPEN_VERSION          700

/* Standard file descriptors */
#define STDIN_FILENO    0
#define STDOUT_FILENO   1
#define STDERR_FILENO   2

/* Values for the "whence" argument to lseek() */
#define SEEK_SET        0       /* Set file offset to offset */
#define SEEK_CUR        1       /* Set file offset to current + offset */
#define SEEK_END        2       /* Set file offset to EOF + offset */

/* Values for access() */
#define F_OK            0       /* Test for existence */
#define X_OK            1       /* Test for execute permission */
#define W_OK            2       /* Test for write permission */
#define R_OK            4       /* Test for read permission */

/* Values for pathconf() and fpathconf() */
#define _PC_LINK_MAX            0
#define _PC_MAX_CANON           1
#define _PC_MAX_INPUT           2
#define _PC_NAME_MAX            3
#define _PC_PATH_MAX            4
#define _PC_PIPE_BUF            5
#define _PC_CHOWN_RESTRICTED    6
#define _PC_NO_TRUNC            7
#define _PC_VDISABLE            8
#define _PC_SYNC_IO             9
#define _PC_ASYNC_IO            10
#define _PC_PRIO_IO             11
#define _PC_FILESIZEBITS        13

/* Values for sysconf() */
#define _SC_ARG_MAX             0
#define _SC_CHILD_MAX           1
#define _SC_CLK_TCK             2
#define _SC_NGROUPS_MAX         3
#define _SC_OPEN_MAX            4
#define _SC_STREAM_MAX          5
#define _SC_TZNAME_MAX          6
#define _SC_JOB_CONTROL         7
#define _SC_SAVED_IDS           8
#define _SC_VERSION             9
#define _SC_PAGESIZE            30
#define _SC_PAGE_SIZE           _SC_PAGESIZE
#define _SC_NPROCESSORS_CONF    83
#define _SC_NPROCESSORS_ONLN    84
#define _SC_PHYS_PAGES          85
#define _SC_AVPHYS_PAGES        86

/* Values for confstr() */
#define _CS_PATH                0
#define _CS_POSIX_V7_ILP32_OFF32_CFLAGS     1
#define _CS_POSIX_V7_ILP32_OFF32_LDFLAGS    2
#define _CS_POSIX_V7_ILP32_OFF32_LIBS       3

/* lockf() operations */
#define F_ULOCK         0       /* Unlock region */
#define F_LOCK          1       /* Lock region for exclusive use */
#define F_TLOCK         2       /* Test and lock (non-blocking) */
#define F_TEST          3       /* Test region for other locks */

/* Null device */
#define _PATH_DEVNULL   "/dev/null"

/* Hostname length */
#ifndef HOST_NAME_MAX
#define HOST_NAME_MAX   64
#endif

/* getopt() support (external variables) */
extern char *optarg;
extern int optind;
extern int opterr;
extern int optopt;

/*
 * Function declarations
 * Note: Many of these are already declared in user.h for userspace.
 * This header is for programs being ported that expect unistd.h.
 */

/* Process creation and control */
pid_t   fork(void);
#define vfork fork          /* no vfork in auxv6; fork is safe here */
int     execve(const char *path, char *const argv[], char *const envp[]);
int     execv(const char *path, char *const argv[]);
int     execvp(const char *file, char *const argv[]);
int     execl(const char *path, const char *arg, ...);
int     execlp(const char *file, const char *arg, ...);
void    _exit(int status) __attribute__((noreturn));

/* Process identification */
pid_t   getpid(void);
pid_t   getppid(void);
pid_t   getpgrp(void);
pid_t   getpgid(pid_t pid);
pid_t   getsid(pid_t pid);
int     setpgid(pid_t pid, pid_t pgid);
pid_t   setsid(void);

/* User and group identification */
uid_t   getuid(void);
uid_t   geteuid(void);
gid_t   getgid(void);
gid_t   getegid(void);
int     setuid(uid_t uid);
int     seteuid(uid_t uid);
int     setgid(gid_t gid);
int     setegid(gid_t gid);
int     setreuid(uid_t ruid, uid_t euid);
int     setregid(gid_t rgid, gid_t egid);
int     getgroups(int gidsetsize, gid_t grouplist[]);
int     setgroups(size_t size, const gid_t *list);
char   *getlogin(void);
int     getlogin_r(char *buf, size_t bufsize);

/* File operations */
int     close(int fd);
ssize_t read(int fd, void *buf, size_t count);
ssize_t write(int fd, const void *buf, size_t count);
ssize_t pread(int fd, void *buf, size_t count, off_t offset);
ssize_t pwrite(int fd, const void *buf, size_t count, off_t offset);
off_t   lseek(int fd, off_t offset, int whence);
int     _llseek(int fd, unsigned int offset_hi, unsigned int offset_lo,
                loff_t *result, unsigned int whence);

/* lseek64 — convenience wrapper: full 64-bit seek, result returned directly. */
#include "sys/types.h"
static inline loff_t
lseek64(int fd, loff_t offset, int whence)
{
  loff_t result = 0;
  if(_llseek(fd, (unsigned int)((unsigned long long)offset >> 32),
             (unsigned int)(offset & 0xffffffffULL), &result,
             (unsigned int)whence) < 0)
    return (loff_t)-1;
  return result;
}
int     dup(int oldfd);
int     dup2(int oldfd, int newfd);
int     pipe(int pipefd[2]);
int     select(int nfds, fd_set *readfds, fd_set *writefds, fd_set *exceptfds,
               struct timeval *timeout);
int     poll(struct pollfd *fds, nfds_t nfds, int timeout);
int     access(const char *path, int mode);
int     faccessat(int fd, const char *path, int mode, int flag);
int     unlink(const char *path);
int     unlinkat(int fd, const char *path, int flag);
int     rmdir(const char *path);
int     link(const char *oldpath, const char *newpath);
int     linkat(int olddirfd, const char *oldpath, int newdirfd, const char *newpath, int flags);
int     symlink(const char *target, const char *linkpath);
int     symlinkat(const char *target, int newdirfd, const char *linkpath);
ssize_t readlink(const char *path, char *buf, size_t bufsiz);
ssize_t readlinkat(int fd, const char *path, char *buf, size_t bufsiz);
int     chown(const char *path, uid_t owner, gid_t group);
int     fchown(int fd, uid_t owner, gid_t group);
int     lchown(const char *path, uid_t owner, gid_t group);
int     chdir(const char *path);
int     fchdir(int fd);
char   *getcwd(char *buf, size_t size);
int     truncate(const char *path, off_t length);
int     ftruncate(int fd, off_t length);
int     utimensat(int dirfd, const char *path, const struct timespec times[2], int flags);
int     futimens(int fd, const struct timespec times[2]);
int     fsync(int fd);
int     fdatasync(int fd);
void    sync(void);
int     isatty(int fd);
char   *ttyname(int fd);
int     ttyname_r(int fd, char *buf, size_t buflen);
int     openpty(int *amaster, int *aslave, char *name,
                const struct termios *termp,
                const struct winsize *winp);

/* Memory */
int     brk(void *addr);
void   *sbrk(intptr_t increment);

/* Sleep and alarm */
unsigned int sleep(unsigned int seconds);
int     usleep(useconds_t usec);
unsigned int alarm(unsigned int seconds);
int     pause(void);

/* Configuration */
long    sysconf(int name);
long    pathconf(const char *path, int name);
long    fpathconf(int fd, int name);
size_t  confstr(int name, char *buf, size_t len);

/* Hostname */
int     gethostname(char *name, size_t len);
int     sethostname(const char *name, size_t len);

/* getopt */
int     getopt(int argc, char *const argv[], const char *optstring);

/* Miscellaneous */
int     nice(int inc);
unsigned int    swab(const void *from, void *to, ssize_t n);
int     chroot(const char *path);

/*
 * Signals — declared here for programs that include unistd.h only.
 * Full declarations live in signal.h.
 */
#ifndef _SIGNAL_H
#include "signal.h"
#endif

/* kill() is declared in user.h; mirror it here for POSIX consumers */
int     kill(pid_t pid, int sig);

/* killpg — send signal to all members of a process group */
int     killpg(int pgrp, int sig);

/*
 * tcsetpgrp / tcgetpgrp — POSIX 2-argument wrappers over the auxv6
 * 1-argument versions that ignore the fd (global tty pgrp).
 */
#ifndef _USER_H   /* avoid double-declaration when user.h pulled in first */
int     tcsetpgrp(int pgid);    /* auxv6 native */
int     tcgetpgrp(void);        /* auxv6 native */
#endif

static inline int tcsetpgrp_posix(int fd, pid_t pgrp) {
  int pg;

  pg = (int)pgrp;
  if(ioctl(fd, TIOCSPGRP, &pg) == 0)
    return 0;
  return tcsetpgrp((int)pgrp);
}

static inline pid_t tcgetpgrp_posix(int fd) {
  int pg;

  if(ioctl(fd, TIOCGPGRP, &pg) == 0)
    return (pid_t)pg;
  return (pid_t)tcgetpgrp();
}

/* For ported code that uses the standard names via macro override */
#ifdef _POSIX_COMPAT_TC
# define tcsetpgrp(fd, pgrp)  tcsetpgrp_posix((fd), (pgrp))
# define tcgetpgrp(fd)        tcgetpgrp_posix((fd))
#endif

#endif /* _UNISTD_H */
