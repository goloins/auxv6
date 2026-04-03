/*
 * string.c - extended string, tokenization, BSD string, and duplication helpers
 * split out of user/ulib.c
 */

#include "types.h"
#include "auxv6/user.h"

char*
strrchr(const char *s, int c)
{
  const char *last;

  last = 0;
  for(; *s; s++)
    if(*s == (char)c)
      last = s;
  if(c == '\0')
    return (char*)s;
  return (char*)last;
}

uint
strspn(const char *s, const char *accept)
{
  const char *p;

  for(p = s; *p; p++) {
    const char *a;

    for(a = accept; *a && *a != *p; a++)
      ;
    if(!*a)
      break;
  }
  return (uint)(p - s);
}

uint
strcspn(const char *s, const char *reject)
{
  const char *p;
  const char *r;

  for(p = s; *p; p++)
    for(r = reject; *r; r++)
      if(*p == *r)
        return (uint)(p - s);
  return (uint)(p - s);
}

char*
strpbrk(const char *s, const char *accept)
{
  for(; *s; s++) {
    const char *a;

    for(a = accept; *a; a++)
      if(*s == *a)
        return (char*)s;
  }
  return 0;
}

char*
strstr(const char *h, const char *n)
{
  uint nl;

  nl = strlen(n);
  if(!nl)
    return (char*)h;
  for(; *h; h++)
    if(*h == *n && strncmp(h, n, nl) == 0)
      return (char*)h;
  return 0;
}

void*
memchr(const void *s, int c, uint n)
{
  const uchar *p;

  p = s;
  while(n-- > 0) {
    if(*p == (uchar)c)
      return (void*)p;
    p++;
  }
  return 0;
}

void*
memrchr(const void *s, int c, uint n)
{
  const uchar *p;

  p = (const uchar*)s + n;
  while(n-- > 0)
    if(*--p == (uchar)c)
      return (void*)p;
  return 0;
}

void*
memcpy(void *dst, const void *src, uint n)
{
  return memmove(dst, src, n);
}

int
memcmp(const void *s1, const void *s2, uint n)
{
  const uchar *a;
  const uchar *b;

  a = s1;
  b = s2;
  while(n-- > 0) {
    if(*a != *b)
      return (int)*a - (int)*b;
    a++;
    b++;
  }
  return 0;
}

void*
mempcpy(void *dst, const void *src, uint n)
{
  return (char*)memmove(dst, src, n) + n;
}

char*
strncpy(char *dst, const char *src, uint n)
{
  char *d;

  d = dst;
  while(n && *src) {
    *d++ = *src++;
    n--;
  }
  while(n--)
    *d++ = '\0';
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
  char *d;

  d = dst;
  while(*d)
    d++;
  while((*d++ = *src++) != 0)
    ;
  return dst;
}

char*
strncat(char *dst, const char *src, uint n)
{
  char *d;

  d = dst;
  while(*d)
    d++;
  while(n-- && *src)
    *d++ = *src++;
  *d = '\0';
  return dst;
}

char*
stpncpy(char *dst, const char *src, uint n)
{
  char *d;
  char *end;

  d = dst;
  while(n && *src) {
    *d++ = *src++;
    n--;
  }
  end = d;
  while(n--)
    *d++ = '\0';
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
  char *tok;

  if(str)
    s = str;
  else if(*saveptr)
    s = *saveptr;
  else
    return 0;

  s += strspn(s, delim);
  if(!*s) {
    *saveptr = 0;
    return 0;
  }

  tok = s;
  s += strcspn(s, delim);
  if(*s)
    *s++ = '\0';
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
  char *s;
  char *tok;

  s = *sp;
  if(!s)
    return 0;
  tok = s;
  s = strpbrk(s, delim);
  if(s)
    *s++ = '\0';
  *sp = s;
  return tok;
}

static int
_lc(int c)
{
  return (c >= 'A' && c <= 'Z') ? c + 32 : c;
}

int
strcasecmp(const char *s1, const char *s2)
{
  while(*s1 && *s2 && _lc((uchar)*s1) == _lc((uchar)*s2)) {
    s1++;
    s2++;
  }
  return _lc((uchar)*s1) - _lc((uchar)*s2);
}

int
strncasecmp(const char *s1, const char *s2, uint n)
{
  while(n && *s1 && *s2 && _lc((uchar)*s1) == _lc((uchar)*s2)) {
    s1++;
    s2++;
    n--;
  }
  return n ? _lc((uchar)*s1) - _lc((uchar)*s2) : 0;
}

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
  uint srclen;

  srclen = strlen(src);
  if(size > 0) {
    uint n;

    n = (srclen < size - 1) ? srclen : size - 1;
    memmove(dst, src, n);
    dst[n] = '\0';
  }
  return srclen;
}

uint
strlcat(char *dst, const char *src, uint size)
{
  uint dlen;
  uint srclen;
  uint room;
  uint n;

  dlen = strnlen(dst, size);
  srclen = strlen(src);
  if(dlen >= size)
    return size + srclen;
  room = size - dlen - 1;
  n = (srclen < room) ? srclen : room;
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

char*
strdup(const char *s)
{
  uint n;
  char *p;

  n = strlen(s) + 1;
  p = malloc(n);
  if(p)
    memmove(p, s, n);
  return p;
}

char*
strndup(const char *s, uint n)
{
  uint len;
  char *p;

  for(len = 0; len < n && s[len]; len++)
    ;
  p = malloc(len + 1);
  if(p) {
    memmove(p, s, len);
    p[len] = '\0';
  }
  return p;
}

/*
 * basename - return the final filename component of a path.
 * Modifies the string in place (strips trailing slashes).
 * Returns "." for an empty or all-slash path.
 */
char *
basename(char *path)
{
  char *p;
  char *last;
  int len;

  if(path == 0 || path[0] == '\0')
    return (char *)".";

  /* Strip trailing slashes */
  len = strlen(path);
  while(len > 1 && path[len - 1] == '/')
    path[--len] = '\0';

  /* Find last '/' */
  last = strrchr(path, '/');
  if(last == 0)
    return path;

  p = last + 1;
  if(*p == '\0')
    return (char *)"/";
  return p;
}

/*
 * dirname - return the directory portion of a path.
 * Modifies the string in place (strips trailing slashes, then the last
 * component). Returns "." if the result would be empty.
 */
char *
dirname(char *path)
{
  char *last;
  int len;

  if(path == 0 || path[0] == '\0')
    return (char *)".";

  /* Strip trailing slashes */
  len = strlen(path);
  while(len > 1 && path[len - 1] == '/')
    path[--len] = '\0';

  /* Find last '/' */
  last = strrchr(path, '/');
  if(last == 0)
    return (char *)".";

  /* Trim trailing slash(es) of the dir portion */
  while(last > path && *last == '/')
    last--;
  last[1] = '\0';

  if(path[0] == '\0')
    return (char *)"/";
  return path;
}