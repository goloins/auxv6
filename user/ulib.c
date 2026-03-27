#include "../include/types.h"
#include "../include/stat.h"
#include "../include/fcntl.h"
#include "../include/user.h"
#include "../include/x86.h"

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
