#include "types.h"
#include "auxv6/user.h"
#include "x86.h"
#include "stddef.h"

/*
 * ulib.c now holds the small xv6-era core runtime primitives.
 * Extended string helpers live in user/string.c and error helpers live in
 * user/errstr.c.
 */

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
strncmp(const char *p, const char *q, size_t n)
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

size_t
strlen(const char *s)
{
  size_t n;

  for(n = 0; s[n]; n++)
    ;
  return n;
}

void*
memset(void *dst, int c, size_t n)
{
  stosb(dst, c, n);
  return dst;
}

char*
strchr(const char *s, int c)
{
  for(; *s; s++)
    if(*s == (char)c)
      return (char*)s;
  return 0;
}

void*
memmove(void *vdst, const void *vsrc, size_t n)
{
  char *dst;
  const char *src;

  dst = vdst;
  src = vsrc;
  while(n-- > 0)
    *dst++ = *src++;
  return vdst;
}


