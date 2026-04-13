/*
 * inet.c - IPv4 presentation/network helpers split out of user/ulib.c
 */

#include "types.h"
#include "auxv6/user.h"
#include "arpa/inet.h"

static int
parse_octet(const char **sp)
{
  const char *s;
  int v;
  int nd;

  s = *sp;
  v = 0;
  nd = 0;
  while(*s >= '0' && *s <= '9') {
    v = v * 10 + (*s - '0');
    if(v > 255)
      return -1;
    s++;
    nd++;
  }
  if(nd == 0)
    return -1;
  *sp = s;
  return v;
}

int
inet_aton(const char *cp, struct in_addr *ap)
{
  const char *s;
  int a;
  int b;
  int c;
  int d;

  s = cp;
  if((a = parse_octet(&s)) < 0 || *s++ != '.')
    return 0;
  if((b = parse_octet(&s)) < 0 || *s++ != '.')
    return 0;
  if((c = parse_octet(&s)) < 0 || *s++ != '.')
    return 0;
  if((d = parse_octet(&s)) < 0 || *s != '\0')
    return 0;

  if(ap)
    ap->s_addr = ((uint)a << 24) | ((uint)b << 16) | ((uint)c << 8) | (uint)d;
  return 1;
}

uint
inet_addr(const char *cp)
{
  struct in_addr a;

  if(!inet_aton(cp, &a))
    return INADDR_NONE;
  return a.s_addr;
}

static char _inet_ntoa_buf[16];

static void
u32_to_decstr(char *out, uint v)
{
  int i;
  int j;
  char tmp[4];

  if(v == 0) {
    out[0] = '0';
    out[1] = '\0';
    return;
  }
  i = 0;
  while(v) {
    tmp[i++] = '0' + (v % 10);
    v /= 10;
  }
  j = 0;
  while(i-- > 0)
    out[j++] = tmp[i];
  out[j] = '\0';
}

char *
inet_ntoa(struct in_addr in)
{
  uint v;
  char fld[4][4];
  char *p;
  int f;

  v = in.s_addr;
  u32_to_decstr(fld[0], (v >> 24) & 0xff);
  u32_to_decstr(fld[1], (v >> 16) & 0xff);
  u32_to_decstr(fld[2], (v >>  8) & 0xff);
  u32_to_decstr(fld[3],  v        & 0xff);
  p = _inet_ntoa_buf;
  for(f = 0; f < 4; f++) {
    char *c;

    for(c = fld[f]; *c; c++)
      *p++ = *c;
    *p++ = (f < 3) ? '.' : '\0';
  }
  return _inet_ntoa_buf;
}

int
inet_pton(int af, const char *src, void *dst)
{
  struct in_addr a;

  if(af != AF_INET)
    return -1;
  if(!inet_aton(src, &a))
    return 0;
  memmove(dst, &a.s_addr, 4);
  return 1;
}

const char *
inet_ntop(int af, const void *src, char *dst, uint size)
{
  uint v;
  struct in_addr a;
  char *tmp;
  uint len;

  if(af != AF_INET)
    return 0;
  memmove(&v, src, 4);
  a.s_addr = v;
  tmp = inet_ntoa(a);
  len = 0;
  while(tmp[len])
    len++;
  if(size < len + 1)
    return 0;
  memmove(dst, tmp, len + 1);
  return dst;
}