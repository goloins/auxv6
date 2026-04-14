/*
 * posix.c - POSIX compatibility shims for auxv6 user space
 *
 * This file now focuses on process, signal, exec, wait, and identity wrappers.
 * Formatting helpers, dirent translation, filesystem wrappers, and PTY helpers
 * live in user/fmt.c, user/dirent.c, user/posix_fs.c, and user/tty.c.
 */

#include "types.h"
#include "stat.h"
#include "errno.h"
#include "stdarg.h"
#include "unistd.h"
#include "../include/signal.h"
#include "auxv6/user.h"
#include "stdlib.h"

int __posix_stat(const char *path, struct stat *buf);

/* malloc/free/open/close/etc. all come from user.h above */

/* -------------------------------------------------------------------------
 * Process / identity stubs
 *
 * auxv6 has getuid/getgid but not euid/egid/groups.  In this single-user
 * kernel there is no difference between real and effective IDs.
 * ------------------------------------------------------------------------- */

uid_t geteuid(void) { return getuid(); }
gid_t getegid(void) { return getgid(); }

int
getgroups(int n, gid_t *groups)
{
  /* Only one group: the primary gid. */
  if(n >= 1) groups[0] = getgid();
  return (n >= 1) ? 1 : 0;
}

/* -------------------------------------------------------------------------
 * environ — global environment variable array
 *
 * auxv6 does not pass envp to main() yet.  Start with an empty environment
 * so that dash initialises without crashing.  The user can export variables
 * to populate it.
 * ------------------------------------------------------------------------- */
static char *_posix_empty_env[] = { 0 };
char **environ = _posix_empty_env;

static const char posix_default_path[] = "/:/bin:/sbin";

int *
__errno_location(void)
{
  return &errno;
}

static const char *
posix_getenv(const char *name)
{
  int namelen;
  char **envp;

  if(name == 0)
    return 0;

  namelen = strlen(name);
  for(envp = environ; envp && *envp; envp++){
    if(strncmp(*envp, name, namelen) == 0 && (*envp)[namelen] == '=')
      return *envp + namelen + 1;
  }
  return 0;
}

static int
posix_build_path(char *dst, int dstsz, const char *dir, const char *file)
{
  int dlen;
  int flen;

  if(dst == 0 || dstsz <= 0 || file == 0)
    return -1;

  if(dir == 0 || *dir == '\0'){
    flen = strlen(file);
    if(flen + 1 > dstsz)
      return -1;
    memmove(dst, file, flen + 1);
    return 0;
  }

  dlen = strlen(dir);
  flen = strlen(file);
  if(dlen + 1 + flen + 1 > dstsz)
    return -1;
  memmove(dst, dir, dlen);
  if(dlen > 0 && dir[dlen - 1] != '/')
    dst[dlen++] = '/';
  memmove(dst + dlen, file, flen + 1);
  return 0;
}

static int
posix_exec_variadic(int use_path, const char *file, const char *arg, va_list ap)
{
  va_list ap_count;
  char **argv;
  const char *s;
  int argc;
  int i;
  int rc;

  argc = 1;
  __builtin_va_copy(ap_count, ap);
  while((s = va_arg(ap_count, const char*)) != 0)
    argc++;
  va_end(ap_count);

  argv = malloc((argc + 1) * sizeof(char*));
  if(argv == 0) {
    errno = ENOMEM;
    return -1;
  }

  argv[0] = (char*)arg;
  i = 1;
  while((s = va_arg(ap, const char*)) != 0)
    argv[i++] = (char*)s;
  argv[i] = 0;

  rc = use_path ? execvp(file, argv) : execv(file, argv);
  free(argv);
  return rc;
}

/* -------------------------------------------------------------------------
 * Signal / exec POSIX wrappers
 * ------------------------------------------------------------------------- */

void
_exit(int status)
{
  _Exit(status);
  __builtin_unreachable();
}

/*
 * signal() — install a signal handler using sigaction.
 * Returns previous handler, or SIG_ERR on failure.
 */
void (*signal(int signum, void (*handler)(int)))(int)
{
  struct sigaction sa, old;
  sa.sa_handler = handler;
  sigemptyset(&sa.sa_mask);
  sa.sa_flags   = 0;
  if(sigaction(signum, &sa, &old) < 0)
    return (void(*)(int))-1;  /* SIG_ERR */
  return old.sa_handler;
}

/* raise() — send signal to the calling process */
int
raise(int sig)
{
  return kill(getpid(), sig);
}

/* killpg() — send signal to all members of process group pgrp. */
int
killpg(int pgrp, int sig)
{
  if(pgrp <= 0) {
    errno = EINVAL;
    return -1;
  }
  return kill(-pgrp, sig);
}

/*
 * pread() — read from a file descriptor at a given offset without
 * changing the file offset (POSIX.1-2001).
 *
 * Implemented via lseek + read + lseek restore. Correct for a
 * single-threaded kernel where no concurrent lseek can interleave.
 */
ssize_t
pread(int fd, void *buf, size_t count, off_t offset)
{
  off_t orig;
  ssize_t n;

  orig = lseek(fd, 0, SEEK_CUR);
  if (orig == (off_t)-1)
    return -1;
  if (lseek(fd, offset, SEEK_SET) == (off_t)-1)
    return -1;
  n = read(fd, buf, count);
  lseek(fd, orig, SEEK_SET);  /* restore; ignore error to preserve read result */
  return n;
}

/*
 * pwrite() — write to a file descriptor at a given offset without
 * changing the file offset (POSIX.1-2001).
 */
ssize_t
pwrite(int fd, const void *buf, size_t count, off_t offset)
{
  off_t orig;
  ssize_t n;

  orig = lseek(fd, 0, SEEK_CUR);
  if (orig == (off_t)-1)
    return -1;
  if (lseek(fd, offset, SEEK_SET) == (off_t)-1)
    return -1;
  n = write(fd, buf, count);
  lseek(fd, orig, SEEK_SET);
  return n;
}

/*
 * execve() — execute a program with environment.
 * auxv6's exec(path, argv) ignores envp at the kernel level; we update
 * the global environ pointer so child's getenv() sees the new env.
 */
int
execve(const char *path, char *const argv[], char *const envp[])
{
  struct stat st;

  if(envp) environ = (char**)envp;
  if(path == 0 || *path == '\0'){
    errno = ENOENT;
    return -1;
  }

  if(__posix_stat(path, &st) < 0)
    return -1;

  errno = 0;
  if(exec((char*)path, (char**)argv) < 0){
    if(errno == 0)
      errno = ENOEXEC;
    return -1;
  }
  return 0;
}

int
execv(const char *path, char *const argv[])
{
  return execve(path, argv, environ);
}

int
execl(const char *path, const char *arg, ...)
{
  va_list ap;
  int rc;

  va_start(ap, arg);
  rc = posix_exec_variadic(0, path, arg, ap);
  va_end(ap);
  return rc;
}

int
execlp(const char *file, const char *arg, ...)
{
  va_list ap;
  int rc;

  va_start(ap, arg);
  rc = posix_exec_variadic(1, file, arg, ap);
  va_end(ap);
  return rc;
}

int
execvp(const char *file, char *const argv[])
{
  const char *path;
  const char *elem;
  const char *next;
  char candidate[256];
  int saw_eacces;

  if(file == 0 || *file == '\0'){
    errno = ENOENT;
    return -1;
  }
  if(strchr(file, '/'))
    return execve(file, argv, environ);

  path = posix_getenv("PATH");
  if(path == 0 || *path == '\0')
    path = posix_default_path;

  saw_eacces = 0;
  while(*path){
    struct stat st;

    elem = path;
    next = strchr(path, ':');
    if(next == 0)
      next = path + strlen(path);
    if(next == elem){
      if(posix_build_path(candidate, sizeof(candidate), 0, file) == 0){
        if(__posix_stat(candidate, &st) == 0)
          return execve(candidate, argv, environ);
        if(errno == EACCES)
          saw_eacces = 1;
      }
    } else {
      char dir[256];
      int dlen = next - elem;

      if(dlen < (int)sizeof(dir)){
        memmove(dir, elem, dlen);
        dir[dlen] = '\0';
        if(posix_build_path(candidate, sizeof(candidate), dir, file) == 0){
          if(__posix_stat(candidate, &st) == 0)
            return execve(candidate, argv, environ);
          if(errno == EACCES)
            saw_eacces = 1;
        }
      } else {
        saw_eacces = 1;
      }
    }
    path = *next ? next + 1 : next;
  }

  errno = saw_eacces ? EACCES : ENOENT;
  return -1;
}

/*
 * wait3() — BSD-compat wait; rusage is ignored on auxv6.
 */
int
wait3(int *status, int options, void *rusage)
{
  (void)rusage;
  return wait4(-1, status, options, 0);
}

/*
 * sigsuspend() — replace signal mask and suspend until a signal arrives.
 * auxv6 has no true sigsuspend syscall; we poll with sleep(1) as a
 * best-effort approximation.  Always returns -1 (EINTR).
 */
int
sigsuspend(const sigset_t *mask)
{
  sigset_t old;
  sigprocmask(SIG_SETMASK, mask, &old);
  sleep(1);
  sigprocmask(SIG_SETMASK, &old, 0);
  errno = EINTR;
  return -1;
}

int
system(const char *command)
{
  struct stat st;
  int pid;
  int status;

  if(command == 0) {
    if(__posix_stat("/bin/sh", &st) == 0)
      return 1;
    if(__posix_stat("/bin/dash", &st) == 0)
      return 1;
    return 0;
  }

  pid = fork();
  if(pid < 0)
    return -1;

  if(pid == 0) {
    char *argv_sh[] = { "sh", "-c", (char*)command, 0 };
    char *argv_dash[] = { "dash", "-c", (char*)command, 0 };

    execve("/bin/sh", argv_sh, environ);
    execve("/bin/dash", argv_dash, environ);
    _exit(127);
  }

  for(;;) {
    if(waitpid(pid, &status, 0) >= 0)
      return status;
    if(errno != EINTR)
      return -1;
  }
}
/*
 * open64() — large-file open alias; auxv6 has no 64-bit file distinction.
 */