#include "types.h"
#include "auxv6/user.h"
#include "x86.h"
#include "stddef.h"

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
stpcpy(char *dst, const char *src)
{
  while((*dst = *src) != '\0') {
    dst++;
    src++;
  }
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


