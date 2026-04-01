#include "../include/types.h"
#include "../include/stat.h"
#include "../include/fcntl.h"
#include "../include/user.h"
#include "../include/x86.h"
#include "../include/stddef.h"

char*
strcpy(char *s, const char *t)
{
  char *os;

  os = s;
  while((*s++ = *t++) != 0)
    ;
  return os;
}

int
strcmp(const char *p, const char *q)
{
  while(*p && *p == *q)
    p++, q++;
  return (uchar)*p - (uchar)*q;
}

int
strncmp(const char *p, const char *q, uint n)
{
  while(n > 0 && *p && *p == *q){
    p++;
    q++;
    n--;
  }
  if(n == 0)
    return 0;
  return (uchar)*p - (uchar)*q;
}

uint
strlen(const char *s)
{
  int n;

  for(n = 0; s[n]; n++)
    ;
  return n;
}

void*
memset(void *dst, int c, uint n)
{
  stosb(dst, c, n);
  return dst;
}

char*
strchr(const char *s, char c)
{
  for(; *s; s++)
    if(*s == c)
      return (char*)s;
  return 0;
}

char*
gets(char *buf, int max)
{
  int i, cc;
  char c;

  for(i=0; i+1 < max; ){
    cc = read(0, &c, 1);
    if(cc < 1)
      break;
    buf[i++] = c;
    if(c == '\n' || c == '\r')
      break;
  }
  buf[i] = '\0';
  return buf;
}

char*
readpass(char *buf, int max)
{
  struct termios oldt;
  struct termios newt;
  int i;
  int cc;
  char c;

  if(max <= 0)
    return buf;

  i = 0;
  if(tcgetattr(0, &oldt) < 0)
    return gets(buf, max);

  newt = oldt;
  newt.c_lflag &= ~(ECHO | ICANON);
  if(tcsetattr(0, TCSANOW, &newt) < 0)
    return gets(buf, max);

  while(i + 1 < max){
    cc = read(0, &c, 1);
    if(cc < 1)
      break;
    if(c == '\r' || c == '\n')
      break;
    if(c == '\b' || c == '\x7f'){
      if(i > 0){
        i--;
        write(1, "\b \b", 3);
      }
      continue;
    }
    if(c == 4)
      break;
    buf[i++] = c;
    write(1, "*", 1);
  }
  buf[i] = 0;
  write(1, "\n", 1);
  tcsetattr(0, TCSANOW, &oldt);
  return buf;
}

int
isatty(int fd)
{
  struct termios t;

  if(fd < 0)
    return 0;
  return (tcgetattr(fd, &t) == 0) ? 1 : 0;
}

char*
ttyname(int fd)
{
  static char name[16];
  struct stat st;

  if(!isatty(fd))
    return 0;

  if(fstat(fd, &st) < 0 || st.st_type != T_DEV)
    return 0;

  if(st.st_major == 1) {
    memmove(name, "/dev/console", 13);
    return name;
  }

  if(st.st_major == 3 && st.st_minor == 0) {
    memmove(name, "/dev/ptmx", 10);
    return name;
  }

  if(st.st_major == 3 && st.st_minor == 1) {
    memmove(name, "/dev/pts/0", 11);
    return name;
  }

  return 0;
}

int
ttyname_r(int fd, char *buf, size_t buflen)
{
  char *name;
  uint need;

  if(buf == 0)
    return -1;

  name = ttyname(fd);
  if(name == 0)
    return -1;

  need = strlen(name) + 1;
  if(buflen < need)
    return -1;

  memmove(buf, name, need);
  return 0;
}

int
atoi(const char *s)
{
  int n;

  n = 0;
  while('0' <= *s && *s <= '9')
    n = n*10 + *s++ - '0';
  return n;
}

void*
memmove(void *vdst, const void *vsrc, int n)
{
  char *dst;
  const char *src;

  dst = vdst;
  src = vsrc;
  while(n-- > 0)
    *dst++ = *src++;
  return vdst;
}

/* =========================================================================
 * Standard C library — string, stdlib, and related functions
 *
 * These were missing from the original xv6 ulib.c.  Added here so that
 * ported software (dash, etc.) can link against a complete runtime.
 * ========================================================================= */

/* errno — global error code variable (set by POSIX wrappers, not syscalls) */
int errno = 0;

/* -------------------------------------------------------------------------
 * String search
 * ------------------------------------------------------------------------- */

char*
strrchr(const char *s, int c)
{
  const char *last = 0;
  for(; *s; s++)
    if(*s == (char)c) last = s;
  if(c == '\0') return (char*)s;
  return (char*)last;
}

uint
strspn(const char *s, const char *accept)
{
  const char *p;
  for(p = s; *p; p++){
    const char *a;
    for(a = accept; *a && *a != *p; a++)
      ;
    if(!*a) break;
  }
  return (uint)(p - s);
}

uint
strcspn(const char *s, const char *reject)
{
  const char *p, *r;
  for(p = s; *p; p++)
    for(r = reject; *r; r++)
      if(*p == *r) return (uint)(p - s);
  return (uint)(p - s);
}

char*
strpbrk(const char *s, const char *accept)
{
  for(; *s; s++){
    const char *a;
    for(a = accept; *a; a++)
      if(*s == *a) return (char*)s;
  }
  return 0;
}

char*
strstr(const char *h, const char *n)
{
  uint nl = strlen(n);
  if(!nl) return (char*)h;
  for(; *h; h++)
    if(*h == *n && strncmp(h, n, nl) == 0) return (char*)h;
  return 0;
}

void*
memchr(const void *s, int c, uint n)
{
  const uchar *p = s;
  while(n-- > 0){
    if(*p == (uchar)c) return (void*)p;
    p++;
  }
  return 0;
}

void*
memrchr(const void *s, int c, uint n)
{
  const uchar *p = (const uchar*)s + n;
  while(n-- > 0)
    if(*--p == (uchar)c) return (void*)p;
  return 0;
}

/* -------------------------------------------------------------------------
 * Memory copy / compare
 * ------------------------------------------------------------------------- */

void*
memcpy(void *dst, const void *src, uint n)
{
  return memmove(dst, src, n);
}

int
memcmp(const void *s1, const void *s2, uint n)
{
  const uchar *a = s1, *b = s2;
  while(n-- > 0){
    if(*a != *b) return (int)*a - (int)*b;
    a++; b++;
  }
  return 0;
}

void*
mempcpy(void *dst, const void *src, uint n)
{
  return (char*)memmove(dst, src, n) + n;
}

/* -------------------------------------------------------------------------
 * String copy / concatenation
 * ------------------------------------------------------------------------- */

char*
strncpy(char *dst, const char *src, uint n)
{
  char *d = dst;
  while(n && *src){ *d++ = *src++; n--; }
  while(n--) *d++ = '\0';
  return dst;
}

char*
strcat(char *dst, const char *src)
{
  char *d = dst;
  while(*d) d++;
  while((*d++ = *src++) != 0)
    ;
  return dst;
}

char*
strncat(char *dst, const char *src, uint n)
{
  char *d = dst;
  while(*d) d++;
  while(n-- && *src) *d++ = *src++;
  *d = '\0';
  return dst;
}

/* stpncpy: copy at most n bytes, pad with NUL; return pointer past last written */
char*
stpncpy(char *dst, const char *src, uint n)
{
  char *d = dst;
  while(n && *src){ *d++ = *src++; n--; }
  char *end = d;
  while(n--) *d++ = '\0';
  return end;
}

uint
strnlen(const char *s, uint maxlen)
{
  uint n;
  for(n = 0; n < maxlen && s[n]; n++)
    ;
  return n;
}

char*
strtok_r(char *str, const char *delim, char **saveptr)
{
  char *s;
  if(str) s = str;
  else if(*saveptr) s = *saveptr;
  else return 0;

  s += strspn(s, delim);
  if(!*s){ *saveptr = 0; return 0; }

  char *tok = s;
  s += strcspn(s, delim);
  if(*s) *s++ = '\0';
  *saveptr = s;
  return tok;
}

static char *_strtok_save;

char*
strtok(char *str, const char *delim)
{
  return strtok_r(str, delim, &_strtok_save);
}

char*
strsep(char **sp, const char *delim)
{
  char *s = *sp;
  char *tok;
  if(!s) return 0;
  tok = s;
  s = strpbrk(s, delim);
  if(s){ *s++ = '\0'; }
  *sp = s;
  return tok;
}

/* -------------------------------------------------------------------------
 * Case-insensitive comparison
 * ------------------------------------------------------------------------- */

static int
_lc(int c)
{
  return (c >= 'A' && c <= 'Z') ? c + 32 : c;
}

int
strcasecmp(const char *s1, const char *s2)
{
  while(*s1 && *s2 && _lc((uchar)*s1) == _lc((uchar)*s2)){
    s1++; s2++;
  }
  return _lc((uchar)*s1) - _lc((uchar)*s2);
}

int
strncasecmp(const char *s1, const char *s2, uint n)
{
  while(n && *s1 && *s2 && _lc((uchar)*s1) == _lc((uchar)*s2)){
    s1++; s2++; n--;
  }
  return n ? _lc((uchar)*s1) - _lc((uchar)*s2) : 0;
}

/* -------------------------------------------------------------------------
 * BSD string functions
 * ------------------------------------------------------------------------- */

void
bzero(void *s, uint n)
{
  memset(s, 0, n);
}

void
bcopy(const void *src, void *dst, uint n)
{
  memmove(dst, src, n);
}

int
bcmp(const void *s1, const void *s2, uint n)
{
  return memcmp(s1, s2, n);
}

uint
strlcpy(char *dst, const char *src, uint size)
{
  uint srclen = strlen(src);
  if(size > 0){
    uint n = (srclen < size - 1) ? srclen : size - 1;
    memmove(dst, src, n);
    dst[n] = '\0';
  }
  return srclen;
}

uint
strlcat(char *dst, const char *src, uint size)
{
  uint dlen = strnlen(dst, size);
  uint srclen = strlen(src);
  if(dlen >= size) return size + srclen;
  uint room = size - dlen - 1;
  uint n = (srclen < room) ? srclen : room;
  memmove(dst + dlen, src, n);
  dst[dlen + n] = '\0';
  return dlen + srclen;
}

char*
index(const char *s, int c)
{
  return strchr(s, c);
}

char*
rindex(const char *s, int c)
{
  return strrchr(s, c);
}

/* -------------------------------------------------------------------------
 * Error description
 * ------------------------------------------------------------------------- */

char*
strerror(int errnum)
{
  static char buf[32];
  switch(errnum){
  case 1:  return "Operation not permitted";
  case 2:  return "No such file or directory";
  case 3:  return "No such process";
  case 4:  return "Interrupted system call";
  case 5:  return "I/O error";
  case 6:  return "No such device or address";
  case 7:  return "Argument list too long";
  case 8:  return "Exec format error";
  case 9:  return "Bad file descriptor";
  case 10: return "No child processes";
  case 11: return "Try again";
  case 12: return "Out of memory";
  case 13: return "Permission denied";
  case 14: return "Bad address";
  case 16: return "Device or resource busy";
  case 17: return "File exists";
  case 18: return "Cross-device link";
  case 19: return "No such device";
  case 20: return "Not a directory";
  case 21: return "Is a directory";
  case 22: return "Invalid argument";
  case 24: return "Too many open files";
  case 25: return "Not a typewriter";
  case 28: return "No space left on device";
  case 36: return "File name too long";
  case 39: return "Directory not empty";
  case 40: return "Too many symbolic links";
  default:
    {
      int n = errnum < 0 ? -errnum : errnum;
      static const char pfx[] = "Unknown error ";
      int i = 31;
      buf[i] = '\0';
      if(n == 0){ buf[--i] = '0'; }
      else while(n){ buf[--i] = '0' + n % 10; n /= 10; }
      i -= sizeof(pfx) - 1;
      if(i < 0) i = 0;
      memmove(buf + i, pfx, sizeof(pfx) - 1);
      return buf + i;
    }
  }
}

char*
strerror_r(int errnum, char *buf, uint buflen)
{
  const char *s = strerror(errnum);
  strlcpy(buf, s, buflen);
  return buf;
}

char*
strsignal(int sig)
{
  static char buf[32];
  switch(sig){
  case 1:  return "Hangup";
  case 2:  return "Interrupt";
  case 3:  return "Quit";
  case 4:  return "Illegal instruction";
  case 6:  return "Aborted";
  case 7:  return "Bus error";
  case 8:  return "Floating point exception";
  case 9:  return "Killed";
  case 11: return "Segmentation fault";
  case 13: return "Broken pipe";
  case 14: return "Alarm clock";
  case 15: return "Terminated";
  case 17: return "Child status changed";
  case 18: return "Continued";
  case 19: return "Stopped (signal)";
  case 20: return "Stopped";
  default:
    {
      int n = sig;
      int i = 31;
      buf[i] = '\0';
      if(n <= 0) n = 0;
      if(n == 0){ buf[--i] = '0'; }
      else while(n){ buf[--i] = '0' + n % 10; n /= 10; }
      static const char pfx[] = "Signal ";
      i -= sizeof(pfx) - 1;
      if(i < 0) i = 0;
      memmove(buf + i, pfx, sizeof(pfx) - 1);
      return buf + i;
    }
  }
}

/* -------------------------------------------------------------------------
 * Numeric string conversion
 * ------------------------------------------------------------------------- */

long
strtol(const char *s, char **endptr, int base)
{
  const char *orig = s;
  long n = 0;
  int neg = 0;

  while(*s == ' ' || *s == '\t') s++;
  if(*s == '-'){ neg = 1; s++; } else if(*s == '+') s++;

  if(base == 0){
    if(*s == '0'){ s++; if(*s=='x'||*s=='X'){ base=16; s++; } else base=8; }
    else base = 10;
  } else if(base == 16 && *s == '0' && (s[1]=='x'||s[1]=='X')) s += 2;

  const char *start = s;
  for(;;){
    int d;
    if(*s >= '0' && *s <= '9') d = *s - '0';
    else if(*s >= 'a' && *s <= 'z') d = *s - 'a' + 10;
    else if(*s >= 'A' && *s <= 'Z') d = *s - 'A' + 10;
    else break;
    if(d >= base) break;
    n = n * base + d;
    s++;
  }
  if(endptr) *endptr = (char*)(s == start ? orig : s);
  return neg ? -n : n;
}

long long
strtoll(const char *s, char **endptr, int base)
{
  const char *orig = s;
  long long n = 0;
  int neg = 0;

  while(*s == ' ' || *s == '\t') s++;
  if(*s == '-'){ neg = 1; s++; } else if(*s == '+') s++;

  if(base == 0){
    if(*s == '0'){ s++; if(*s=='x'||*s=='X'){ base=16; s++; } else base=8; }
    else base = 10;
  } else if(base == 16 && *s == '0' && (s[1]=='x'||s[1]=='X')) s += 2;

  const char *start = s;
  for(;;){
    int d;
    if(*s >= '0' && *s <= '9') d = *s - '0';
    else if(*s >= 'a' && *s <= 'z') d = *s - 'a' + 10;
    else if(*s >= 'A' && *s <= 'Z') d = *s - 'A' + 10;
    else break;
    if(d >= base) break;
    n = n * base + d;
    s++;
  }
  if(endptr) *endptr = (char*)(s == start ? orig : s);
  return neg ? -n : n;
}

unsigned long
strtoul(const char *s, char **endptr, int base)
{
  const char *orig = s;
  unsigned long n = 0;

  while(*s == ' ' || *s == '\t') s++;
  if(*s == '+') s++;
  else if(*s == '-') s++;   /* silently ignored for unsigned */

  if(base == 0){
    if(*s == '0'){ s++; if(*s=='x'||*s=='X'){ base=16; s++; } else base=8; }
    else base = 10;
  } else if(base == 16 && *s == '0' && (s[1]=='x'||s[1]=='X')) s += 2;

  const char *start = s;
  for(;;){
    int d;
    if(*s >= '0' && *s <= '9') d = *s - '0';
    else if(*s >= 'a' && *s <= 'z') d = *s - 'a' + 10;
    else if(*s >= 'A' && *s <= 'Z') d = *s - 'A' + 10;
    else break;
    if(d >= base) break;
    n = n * base + d;
    s++;
  }
  if(endptr) *endptr = (char*)(s == start ? orig : s);
  return n;
}

unsigned long long
strtoull(const char *s, char **endptr, int base)
{
  const char *orig = s;
  unsigned long long n = 0;

  while(*s == ' ' || *s == '\t') s++;
  if(*s == '+') s++;
  else if(*s == '-') s++;

  if(base == 0){
    if(*s == '0'){ s++; if(*s=='x'||*s=='X'){ base=16; s++; } else base=8; }
    else base = 10;
  } else if(base == 16 && *s == '0' && (s[1]=='x'||s[1]=='X')) s += 2;

  const char *start = s;
  for(;;){
    int d;
    if(*s >= '0' && *s <= '9') d = *s - '0';
    else if(*s >= 'a' && *s <= 'z') d = *s - 'a' + 10;
    else if(*s >= 'A' && *s <= 'Z') d = *s - 'A' + 10;
    else break;
    if(d >= base) break;
    n = n * base + d;
    s++;
  }
  if(endptr) *endptr = (char*)(s == start ? orig : s);
  return n;
}

long
atol(const char *s)
{
  return strtol(s, 0, 10);
}

long long
atoll(const char *s)
{
  return strtoll(s, 0, 10);
}

/* -------------------------------------------------------------------------
 * Integer arithmetic
 * ------------------------------------------------------------------------- */

int
abs(int j)
{
  return j < 0 ? -j : j;
}

long
labs(long j)
{
  return j < 0 ? -j : j;
}

long long
llabs(long long j)
{
  return j < 0 ? -j : j;
}

/* -------------------------------------------------------------------------
 * Program termination
 * ------------------------------------------------------------------------- */

static void (*_atexit_fns[32])(void);
static int   _atexit_count;

int
atexit(void (*func)(void))
{
  if(_atexit_count >= 32) return -1;
  _atexit_fns[_atexit_count++] = func;
  return 0;
}

void
abort(void)
{
  kill(getpid(), 6);  /* SIGABRT = 6 */
  exit();             /* fallback if kill fails */
  __builtin_unreachable();
}

/* -------------------------------------------------------------------------
 * Searching and sorting
 * ------------------------------------------------------------------------- */

void*
bsearch(const void *key, const void *base, uint nmemb, uint size,
        int (*compar)(const void*, const void*))
{
  uint lo = 0, hi = nmemb;
  while(lo < hi){
    uint mid = lo + (hi - lo) / 2;
    const void *el = (const char*)base + mid * size;
    int cmp = compar(key, el);
    if(cmp == 0) return (void*)el;
    if(cmp < 0) hi = mid;
    else lo = mid + 1;
  }
  return 0;
}

static void
_swap(char *a, char *b, uint n)
{
  char t;
  while(n--){ t=*a; *a++=*b; *b++=t; }
}

void
qsort(void *base, uint nmemb, uint size,
      int (*compar)(const void*, const void*))
{
  if(nmemb <= 1) return;
  uint i, j;
  for(i = 1; i < nmemb; i++)
    for(j = i; j > 0; j--){
      char *a = (char*)base + (j-1)*size;
      char *b = (char*)base + j*size;
      if(compar(a, b) > 0) _swap(a, b, size);
      else break;
    }
}

/* -------------------------------------------------------------------------
 * Random number generation
 * ------------------------------------------------------------------------- */

static unsigned int _rand_seed = 1;

int __attribute__((weak))
rand(void)
{
  _rand_seed = _rand_seed * 1103515245u + 12345u;
  return (int)((_rand_seed >> 16) & 0x7fff);
}

void __attribute__((weak))
srand(unsigned int seed)
{
  _rand_seed = seed;
}

/* -------------------------------------------------------------------------
 * String allocation
 * ------------------------------------------------------------------------- */

char*
strdup(const char *s)
{
  uint n = strlen(s) + 1;
  char *p = malloc(n);
  if(p) memmove(p, s, n);
  return p;
}

char*
strndup(const char *s, uint n)
{
  uint len;
  char *p;
  for(len = 0; len < n && s[len]; len++) ;
  p = malloc(len + 1);
  if(p){ memmove(p, s, len); p[len] = '\0'; }
  return p;
}

/* -------------------------------------------------------------------------
 * Environment variables
 *
 * environ is defined in posix.c; always linked via ULIB.
 * ------------------------------------------------------------------------- */

extern char **environ;

static uint
_envcount(char **env)
{
  uint n = 0;
  if(env) while(env[n]) n++;
  return n;
}

char*
getenv(const char *name)
{
  uint len = strlen(name);
  char **ep;
  if(!environ) return 0;
  for(ep = environ; *ep; ep++)
    if(strncmp(*ep, name, len) == 0 && (*ep)[len] == '=')
      return *ep + len + 1;
  return 0;
}

int
putenv(char *string)
{
  uint len;
  uint n;
  uint i;
  for(len = 0; string[len] && string[len] != '='; len++) ;
  n = _envcount(environ);
  i = 0;
  for(i = 0; i < n; i++){
    if(strncmp(environ[i], string, len) == 0 && environ[i][len] == '='){
      environ[i] = string;
      return 0;
    }
  }
  {
    char **nenv = malloc((n + 2) * sizeof(char*));
    if(!nenv) return -1;
    for(i = 0; i < n; i++) nenv[i] = environ[i];
    nenv[n]   = string;
    nenv[n+1] = 0;
    environ = nenv;
  }
  return 0;
}

int
setenv(const char *name, const char *value, int overwrite)
{
  uint nlen = strlen(name);
  uint vlen = strlen(value);
  char **ep;
  uint i = 0;
  if(environ){
    for(ep = environ; *ep; ep++, i++){
      if(strncmp(*ep, name, nlen) == 0 && (*ep)[nlen] == '='){
        if(!overwrite) return 0;
        {
          char *slot = malloc(nlen + 1 + vlen + 1);
          if(!slot) return -1;
          strcpy(slot, name);
          slot[nlen] = '=';
          strcpy(slot + nlen + 1, value);
          environ[i] = slot;
        }
        return 0;
      }
    }
  }
  {
    char *entry = malloc(nlen + 1 + vlen + 1);
    if(!entry) return -1;
    strcpy(entry, name);
    entry[nlen] = '=';
    strcpy(entry + nlen + 1, value);
    return putenv(entry);
  }
}

int
unsetenv(const char *name)
{
  uint len = strlen(name);
  uint n = _envcount(environ);
  uint i;
  for(i = 0; i < n; i++){
    if(strncmp(environ[i], name, len) == 0 && environ[i][len] == '='){
      for(; i < n; i++) environ[i] = environ[i+1];
      return 0;
    }
  }
  return 0;
}

int
clearenv(void)
{
  static char *_empty[] = { 0 };
  environ = _empty;
  return 0;
}

/* end of ulib.c */

