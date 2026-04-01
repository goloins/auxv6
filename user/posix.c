/*
 * posix.c - POSIX compatibility shims for auxv6 user space
 *
 * Implements:
 *   opendir / readdir / closedir / rewinddir
 *   vsnprintf / snprintf / sprintf / vsprintf / sscanf
 *
 * The kernel's struct dirent uses {ushort inum; char name[DIRSIZ]} (DIRSIZ=14).
 * getdents() fills an array of those kernel dirents.
 * We translate each one into the POSIX struct dirent {ino_t d_ino; char d_name[]}.
 */

#include "../include/types.h"
#include "../include/fcntl.h"
#include "../include/stat.h"
#include "../include/errno.h"
#include "../include/user.h"
#include "../include/posix/dirent.h"
#include "../include/posix/stdarg.h"
#include "../include/posix/sys/ioctl.h"

#ifndef AT_FDCWD
#define AT_FDCWD (-100)
#endif

#ifndef AT_EACCESS
#define AT_EACCESS 0x200
#endif

#ifndef F_OK
#define F_OK 0
#endif

#ifndef X_OK
#define X_OK 1
#endif

#ifndef W_OK
#define W_OK 2
#endif

#ifndef R_OK
#define R_OK 4
#endif

/* malloc/free/open/close/etc. all come from user.h above */

/* -------------------------------------------------------------------------
 * vsnprintf / snprintf / sprintf / vsprintf / sscanf
 *
 * Minimal but complete implementation covering:
 *   %d  %i  (int, signed)
 *   %u       (unsigned int)
 *   %o       (octal)
 *   %x  %X   (hex lower/upper)
 *   %p       (pointer as hex)
 *   %s       (string)
 *   %c       (character)
 *   %%       (literal %)
 *   Flags:   - 0 +  (space) #
 *   Width:   numeric or *
 *   Precision: .numeric or .*
 *   Length:  h hh l ll z t (only affect signedness; all fit in int/long on 32-bit)
 * ------------------------------------------------------------------------- */

/* Append one character to the output buffer; always updates total count. */
#define EMIT(c) do { \
    if(pos < (int)size - 1) buf[pos] = (c); \
    pos++; \
} while(0)

static void
emit_str(char *buf, int *posp, int size, const char *s, int slen,
         int left, int width, int have_prec, int prec)
{
  int n;
  int pad;
  int i;

  if(s == 0) s = "(null)";
  n = 0;
  while(s[n]) n++;
  if(have_prec && prec < n) n = prec;
  (void)slen;

  pad = width - n;
  if(pad < 0) pad = 0;

  if(!left)
    for(i = 0; i < pad; i++) { if(*posp < size-1) buf[*posp] = ' '; (*posp)++; }
  for(i = 0; i < n; i++) { if(*posp < size-1) buf[*posp] = s[i]; (*posp)++; }
  if(left)
    for(i = 0; i < pad; i++) { if(*posp < size-1) buf[*posp] = ' '; (*posp)++; }
}

static void
u64_divmod_small(unsigned long long n, unsigned base,
                 unsigned long long *q_out, unsigned *r_out)
{
  unsigned long long q;
  unsigned long long r;
  int i;

  q = 0;
  r = 0;
  for(i = 63; i >= 0; i--){
    r = (r << 1) | ((n >> i) & 1ULL);
    if(r >= (unsigned long long)base){
      r -= (unsigned long long)base;
      q |= (1ULL << i);
    }
  }

  if(q_out)
    *q_out = q;
  if(r_out)
    *r_out = (unsigned)r;
}

static void
emit_uint(char *buf, int *posp, int size, unsigned long long v,
          int base, int upper, int neg, int left, int width,
          int zero_pad, int have_prec, int prec, int alt, int blank, int plus)
{
  char tmp[30];
  int  dn = 0;
  int  prefix_len = 0;
  char prefix[4];
  int  total, pad, i;
  char padch;
  static const char *lo = "0123456789abcdef";
  static const char *hi = "0123456789ABCDEF";
  const char *digits = upper ? hi : lo;

  /* Build digits in reverse */
  if(v == 0 && !(have_prec && prec == 0)) {
    tmp[dn++] = '0';
  } else {
    unsigned long long t = v;
    while(t > 0) {
      unsigned rem;
      unsigned long long q;
      u64_divmod_small(t, (unsigned)base, &q, &rem);
      tmp[dn++] = digits[rem];
      t = q;
    }
  }

  /* Build prefix */
  if(neg) { prefix[prefix_len++] = '-'; }
  else if(plus) { prefix[prefix_len++] = '+'; }
  else if(blank) { prefix[prefix_len++] = ' '; }
  if(alt && base == 16 && v) { prefix[prefix_len++] = '0'; prefix[prefix_len++] = upper?'X':'x'; }
  if(alt && base == 8  && (dn==0 || tmp[dn-1]!='0')) { prefix[prefix_len++] = '0'; }

  /* Calculate widths */
  int nzeros = 0;
  if(have_prec && prec > dn) nzeros = prec - dn;
  total = prefix_len + nzeros + dn;
  pad = width - total;
  if(pad < 0) pad = 0;

  padch = (zero_pad && !left && !have_prec) ? '0' : ' ';

  if(!left && padch == ' ')
    for(i=0;i<pad;i++){ if(*posp<size-1) buf[*posp]=' '; (*posp)++; }
  for(i=0;i<prefix_len;i++){ if(*posp<size-1) buf[*posp]=prefix[i]; (*posp)++; }
  if(!left && padch == '0')
    for(i=0;i<pad;i++){ if(*posp<size-1) buf[*posp]='0'; (*posp)++; }
  for(i=0;i<nzeros;i++){ if(*posp<size-1) buf[*posp]='0'; (*posp)++; }
  for(i=dn-1;i>=0;i--){ if(*posp<size-1) buf[*posp]=tmp[i]; (*posp)++; }
  if(left)
    for(i=0;i<pad;i++){ if(*posp<size-1) buf[*posp]=' '; (*posp)++; }
}

int
vsnprintf(char *buf, size_t size, const char *fmt, va_list ap)
{
  int pos = 0;
  int c;

  if(size == 0) buf = 0;

  while((c = *fmt++) != 0) {
    if(c != '%') {
      if(buf && pos < (int)size-1) buf[pos] = (char)c;
      pos++;
      continue;
    }

    /* Flags */
    int left=0, plus=0, blank=0, alt=0, zero_pad=0;
    for(;;) {
      c = *fmt++;
      if     (c=='-') left=1;
      else if(c=='+') plus=1;
      else if(c==' ') blank=1;
      else if(c=='#') alt=1;
      else if(c=='0') zero_pad=1;
      else break;
    }
    if(left) zero_pad=0;

    /* Width */
    int width = 0;
    if(c == '*') { width = va_arg(ap, int); if(width<0){left=1;width=-width;} c=*fmt++; }
    else { while(c>='0'&&c<='9'){width=width*10+(c-'0');c=*fmt++;} }

    /* Precision */
    int have_prec=0, prec=0;
    if(c=='.') {
      have_prec=1; c=*fmt++;
      if(c=='*'){prec=va_arg(ap,int);if(prec<0){have_prec=0;prec=0;}c=*fmt++;}
      else { while(c>='0'&&c<='9'){prec=prec*10+(c-'0');c=*fmt++;} }
    }

    /* Length */
    int is_ll=0, is_l=0, is_hh=0, is_h=0, is_z=0;
    if(c=='l'){c=*fmt++;if(c=='l'){is_ll=1;c=*fmt++;}else is_l=1;}
    else if(c=='h'){c=*fmt++;if(c=='h'){is_hh=1;c=*fmt++;}else is_h=1;}
    else if(c=='z'){is_z=1;c=*fmt++;}
    else if(c=='t'||c=='j'){c=*fmt++;}  /* treat as int */

    /* Conversion */
    if(c=='d'||c=='i') {
      long long v;
      if(is_ll)      v=va_arg(ap,long long);
      else if(is_l)  v=va_arg(ap,long);
      else if(is_hh) v=(signed char)va_arg(ap,int);
      else if(is_h)  v=(short)va_arg(ap,int);
      else if(is_z)  v=(long)va_arg(ap,size_t);
      else           v=va_arg(ap,int);
      unsigned long long uv = (v<0)?(unsigned long long)(-v):(unsigned long long)v;
      emit_uint(buf,&pos,(int)size,uv,10,0,v<0,left,width,zero_pad,have_prec,prec,0,blank,plus);
    } else if(c=='u'||c=='o'||c=='x'||c=='X') {
      unsigned long long v;
      int base = (c=='o')?8:(c=='u')?10:16;
      int upper = (c=='X');
      if(is_ll)      v=va_arg(ap,unsigned long long);
      else if(is_l)  v=va_arg(ap,unsigned long);
      else if(is_hh) v=(unsigned char)va_arg(ap,unsigned);
      else if(is_h)  v=(unsigned short)va_arg(ap,unsigned);
      else if(is_z)  v=va_arg(ap,size_t);
      else           v=va_arg(ap,unsigned);
      emit_uint(buf,&pos,(int)size,v,base,upper,0,left,width,zero_pad,have_prec,prec,alt,0,0);
    } else if(c=='p') {
      unsigned long v = (unsigned long)va_arg(ap,void*);
      emit_uint(buf,&pos,(int)size,(unsigned long long)v,16,0,0,left,width,1,1,8,1,0,0);
    } else if(c=='s') {
      const char *s = va_arg(ap,const char*);
      emit_str(buf,&pos,(int)size,s,0,left,width,have_prec,prec);
    } else if(c=='c') {
      char ch = (char)va_arg(ap,int);
      int pad = width-1; if(pad<0) pad=0;
      if(!left) { int i; for(i=0;i<pad;i++){if(pos<(int)size-1)buf[pos]=' ';pos++;} }
      if(pos<(int)size-1) buf[pos]=(char)ch;
      pos++;
      if(left)  { int i; for(i=0;i<pad;i++){if(pos<(int)size-1)buf[pos]=' ';pos++;} }
    } else if(c=='%') {
      if(pos<(int)size-1) buf[pos]='%';
      pos++;
    } else if(c=='n') {
      /* Write count — allow it but this is a potential security issue;
       * only included because dash may use it indirectly via format specs.
       * We simply skip assigning since it's rarely needed. */
      (void)va_arg(ap,int*);
    } else {
      /* Unknown: pass through */
      if(pos<(int)size-1){ buf[pos]='%'; pos++; }
      if(pos<(int)size-1){ buf[pos]=(char)c; pos++; }
    }
  }

  if(buf) {
    if(pos < (int)size) buf[pos] = '\0';
    else if(size > 0)   buf[size-1] = '\0';
  }
  return pos;
}

int
snprintf(char *buf, size_t size, const char *fmt, ...)
{
  va_list ap;
  int n;
  va_start(ap, fmt);
  n = vsnprintf(buf, size, fmt, ap);
  va_end(ap);
  return n;
}

int
vsprintf(char *buf, const char *fmt, va_list ap)
{
  /* No size limit — caller must ensure buffer is large enough */
  return vsnprintf(buf, (size_t)0x7fffffff, fmt, ap);
}

int
sprintf(char *buf, const char *fmt, ...)
{
  va_list ap;
  int n;
  va_start(ap, fmt);
  n = vsprintf(buf, fmt, ap);
  va_end(ap);
  return n;
}

int
sscanf(const char *str, const char *fmt, ...)
{
  /* Stub — dash does not rely on sscanf at runtime */
  (void)str; (void)fmt;
  return 0;
}


#define KDIRENT_BUF  16

/* Kernel-level dirent as seen by getdents(): matches fs.h */
struct kdirent {
  unsigned short inum;
  char name[14];   /* DIRSIZ = 14 */
};

DIR *
opendir(const char *path)
{
  int fd;
  DIR *dp;

  fd = open(path, O_RDONLY);
  if(fd < 0)
    return 0;

  dp = (DIR *)malloc(sizeof(DIR));
  if(dp == 0){
    close(fd);
    return 0;
  }
  dp->dd_fd    = fd;
  dp->dd_loc   = 0;
  dp->dd_size  = 0;
  return dp;
}

struct dirent *
readdir(DIR *dp)
{
  struct kdirent kbuf[KDIRENT_BUF];
  int n;
  int i;

  if(dp == 0)
    return 0;

  for(;;){
    /* Return next already-buffered entry */
    while(dp->dd_loc < dp->dd_size){
      struct kdirent *kd = (struct kdirent *)dp->dd_buf + dp->dd_loc;
      dp->dd_loc++;
      if(kd->inum == 0)
        continue;  /* deleted slot, skip */
      dp->dd_ent.d_ino = (ino_t)kd->inum;
      memmove(dp->dd_ent.d_name, kd->name, 14);
      dp->dd_ent.d_name[14] = '\0';
      return &dp->dd_ent;
    }

    /* Buffer exhausted — fetch more */
    n = getdents(dp->dd_fd, (struct dirent *)kbuf, KDIRENT_BUF);
    if(n <= 0)
      return 0;

    /* Copy raw kernel bytes into our buffer */
    for(i = 0; i < n; i++)
      *((struct kdirent *)dp->dd_buf + i) = kbuf[i];

    dp->dd_loc  = 0;
    dp->dd_size = n;
  }
}

int
closedir(DIR *dp)
{
  int fd;

  if(dp == 0)
    return -1;
  fd = dp->dd_fd;
  free(dp);
  return close(fd);
}

void
rewinddir(DIR *dp)
{
  if(dp == 0)
    return;
  /* Seek the underlying fd back to the beginning */
  lseek(dp->dd_fd, 0, 0 /* SEEK_SET */);
  dp->dd_loc  = 0;
  dp->dd_size = 0;
}

/* -------------------------------------------------------------------------
 * String helpers not yet in ulib.c
 * ------------------------------------------------------------------------- */

char *
stpcpy(char *d, const char *s)
{
  while((*d = *s) != '\0') { d++; s++; }
  return d;
}

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
 * sysconf — system configuration constants
 * ------------------------------------------------------------------------- */
long
sysconf(int name)
{
  switch(name) {
  case 2:   return 100;   /* _SC_CLK_TCK   — 100 Hz */
  case 4:   return 20;    /* _SC_OPEN_MAX  — same as NOFILE in param.h */
  case 5:   return 256;   /* _SC_CHILD_MAX */
  case 8:   return 4096;  /* _SC_PAGESIZE  — 4 KB pages */
  case 30:  return 256;   /* _SC_NAME_MAX  */
  case 36:  return 256;   /* _SC_PATH_MAX  */
  default:  return -1;
  }
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

static void
posix_fixup_mode_from_type(struct stat *st)
{
  int ftype;

  if(st == 0)
    return;
  if((st->st_mode & M_IFMT) != 0)
    return;

  ftype = 0;
  switch(st->st_type){
  case T_FILE:
    ftype = M_IFREG;
    break;
  case T_DIR:
    ftype = M_IFDIR;
    break;
  case T_DEV:
    ftype = M_IFCHR;
    break;
  default:
    break;
  }
  st->st_mode = (st->st_mode & 07777) | ftype;
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
posix_exec_access_mode(const struct stat *st, int mode)
{
  int bits;

  if(st == 0)
    return -1;
  if(mode == F_OK)
    return 0;

  if(geteuid() == 0){
    if((mode & X_OK) && ((st->st_mode & M_IFMT) != M_IFDIR) &&
       (st->st_mode & (M_IXUSR | M_IXGRP | M_IXOTH)) == 0)
      return -1;
    return 0;
  }

  if(geteuid() == st->st_uid)
    bits = (st->st_mode >> 6) & 07;
  else if(getegid() == st->st_gid)
    bits = (st->st_mode >> 3) & 07;
  else
    bits = st->st_mode & 07;

  if((mode & R_OK) && (bits & 04) == 0)
    return -1;
  if((mode & W_OK) && (bits & 02) == 0)
    return -1;
  if((mode & X_OK) && (bits & 01) == 0)
    return -1;
  return 0;
}

int
__posix_stat(const char *path, struct stat *buf)
{
  struct stat lst;

  errno = 0;
  if(stat(path, buf) < 0){
    if(errno == 0){
      // Best-effort mapping: if final component is a symlink and follow failed,
      // report ELOOP (common Unix behavior for symlink loops).
      if(lstat(path, &lst) == 0 && lst.st_type == T_SYMLINK)
        errno = ELOOP;
      else
        errno = ENOENT;
    }
    return -1;
  }
  posix_fixup_mode_from_type(buf);
  return 0;
}

int
__posix_fstat(int fd, struct stat *buf)
{
  errno = 0;
  if(fstat(fd, buf) < 0){
    if(errno == 0)
      errno = EBADF;
    return -1;
  }
  posix_fixup_mode_from_type(buf);
  return 0;
}

int
__posix_lstat(const char *path, struct stat *buf)
{
  errno = 0;
  if(lstat(path, buf) < 0){
    if(errno == 0)
      errno = ENOENT;
    return -1;
  }
  posix_fixup_mode_from_type(buf);
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

/* -------------------------------------------------------------------------
 * Signal / exec POSIX wrappers
 * ------------------------------------------------------------------------- */

void
_exit(int status)
{
  (void)status;
  exit();
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
  sa.sa_mask    = 0;
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

char *
__posix_getcwd(char *buf, size_t size)
{
  if(buf == 0 || size == 0){
    errno = EINVAL;
    return 0;
  }
  errno = 0;
  if(getcwd(buf, (int)size) < 0){
    if(errno == 0)
      errno = ENOENT;
    return 0;
  }
  return buf;
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

int
faccessat(int fd, const char *path, int mode, int flag)
{
  struct stat st;

  if(fd != AT_FDCWD){
    errno = ENOSYS;
    return -1;
  }
  if((flag & ~AT_EACCESS) != 0){
    errno = EINVAL;
    return -1;
  }
  if((mode & ~(F_OK | R_OK | W_OK | X_OK)) != 0){
    errno = EINVAL;
    return -1;
  }
  if(__posix_stat(path, &st) < 0)
    return -1;
  if(posix_exec_access_mode(&st, mode) < 0){
    errno = EACCES;
    return -1;
  }
  return 0;
}

int
access(const char *path, int mode)
{
  return faccessat(AT_FDCWD, path, mode, 0);
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
/*
 * open64() — large-file open alias; auxv6 has no 64-bit file distinction.
 */
int
open64(const char *path, int flags, ...)
{
  return open((char*)path, flags);
}

int
openpty(int *amaster, int *aslave, char *name,
        const struct termios *termp,
        const struct winsize *winp)
{
  int mfd;
  int sfd;

  if(amaster == 0 || aslave == 0) {
    errno = EINVAL;
    return -1;
  }

  mfd = open("/dev/ptmx", O_RDWR);
  if(mfd < 0) {
    errno = ENOENT;
    return -1;
  }

  sfd = open("/dev/pts/0", O_RDWR);
  if(sfd < 0) {
    close(mfd);
    errno = ENOENT;
    return -1;
  }

  if(termp) {
    if(ioctl(sfd, TCSETS, (void*)termp) < 0) {
      close(sfd);
      close(mfd);
      errno = EINVAL;
      return -1;
    }
  }

  if(winp) {
    if(ioctl(sfd, TIOCSWINSZ, (void*)winp) < 0) {
      close(sfd);
      close(mfd);
      errno = EINVAL;
      return -1;
    }
  }

  if(name)
    strcpy(name, "/dev/pts/0");

  *amaster = mfd;
  *aslave = sfd;
  return 0;
}