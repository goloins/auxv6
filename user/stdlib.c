/*
 * stdlib.c - non-string standard library helpers split out of user/ulib.c
 */

#include "types.h"
#include "errno.h"
#include "auxv6/user.h"
#include "stdlib.h"

int
atoi(const char *s)
{
  int n;

  n = 0;
  while('0' <= *s && *s <= '9')
    n = n * 10 + *s++ - '0';
  return n;
}

long
strtol(const char *s, char **endptr, int base)
{
  const char *orig;
  const char *start;
  long n;
  int neg;

  orig = s;
  n = 0;
  neg = 0;

  while(*s == ' ' || *s == '\t')
    s++;
  if(*s == '-') {
    neg = 1;
    s++;
  } else if(*s == '+') {
    s++;
  }

  if(base == 0) {
    if(*s == '0') {
      s++;
      if(*s == 'x' || *s == 'X') {
        base = 16;
        s++;
      } else {
        base = 8;
      }
    } else {
      base = 10;
    }
  } else if(base == 16 && *s == '0' && (s[1] == 'x' || s[1] == 'X')) {
    s += 2;
  }

  start = s;
  for(;;) {
    int d;

    if(*s >= '0' && *s <= '9')
      d = *s - '0';
    else if(*s >= 'a' && *s <= 'z')
      d = *s - 'a' + 10;
    else if(*s >= 'A' && *s <= 'Z')
      d = *s - 'A' + 10;
    else
      break;
    if(d >= base)
      break;
    n = n * base + d;
    s++;
  }
  if(endptr)
    *endptr = (char*)(s == start ? orig : s);
  return neg ? -n : n;
}

long long
strtoll(const char *s, char **endptr, int base)
{
  const char *orig;
  const char *start;
  long long n;
  int neg;

  orig = s;
  n = 0;
  neg = 0;

  while(*s == ' ' || *s == '\t')
    s++;
  if(*s == '-') {
    neg = 1;
    s++;
  } else if(*s == '+') {
    s++;
  }

  if(base == 0) {
    if(*s == '0') {
      s++;
      if(*s == 'x' || *s == 'X') {
        base = 16;
        s++;
      } else {
        base = 8;
      }
    } else {
      base = 10;
    }
  } else if(base == 16 && *s == '0' && (s[1] == 'x' || s[1] == 'X')) {
    s += 2;
  }

  start = s;
  for(;;) {
    int d;

    if(*s >= '0' && *s <= '9')
      d = *s - '0';
    else if(*s >= 'a' && *s <= 'z')
      d = *s - 'a' + 10;
    else if(*s >= 'A' && *s <= 'Z')
      d = *s - 'A' + 10;
    else
      break;
    if(d >= base)
      break;
    n = n * base + d;
    s++;
  }
  if(endptr)
    *endptr = (char*)(s == start ? orig : s);
  return neg ? -n : n;
}

unsigned long
strtoul(const char *s, char **endptr, int base)
{
  const char *orig;
  const char *start;
  unsigned long n;

  orig = s;
  n = 0;

  while(*s == ' ' || *s == '\t')
    s++;
  if(*s == '+')
    s++;
  else if(*s == '-')
    s++;

  if(base == 0) {
    if(*s == '0') {
      s++;
      if(*s == 'x' || *s == 'X') {
        base = 16;
        s++;
      } else {
        base = 8;
      }
    } else {
      base = 10;
    }
  } else if(base == 16 && *s == '0' && (s[1] == 'x' || s[1] == 'X')) {
    s += 2;
  }

  start = s;
  for(;;) {
    int d;

    if(*s >= '0' && *s <= '9')
      d = *s - '0';
    else if(*s >= 'a' && *s <= 'z')
      d = *s - 'a' + 10;
    else if(*s >= 'A' && *s <= 'Z')
      d = *s - 'A' + 10;
    else
      break;
    if(d >= base)
      break;
    n = n * base + d;
    s++;
  }
  if(endptr)
    *endptr = (char*)(s == start ? orig : s);
  return n;
}

unsigned long long
strtoull(const char *s, char **endptr, int base)
{
  const char *orig;
  const char *start;
  unsigned long long n;

  orig = s;
  n = 0;

  while(*s == ' ' || *s == '\t')
    s++;
  if(*s == '+')
    s++;
  else if(*s == '-')
    s++;

  if(base == 0) {
    if(*s == '0') {
      s++;
      if(*s == 'x' || *s == 'X') {
        base = 16;
        s++;
      } else {
        base = 8;
      }
    } else {
      base = 10;
    }
  } else if(base == 16 && *s == '0' && (s[1] == 'x' || s[1] == 'X')) {
    s += 2;
  }

  start = s;
  for(;;) {
    int d;

    if(*s >= '0' && *s <= '9')
      d = *s - '0';
    else if(*s >= 'a' && *s <= 'z')
      d = *s - 'a' + 10;
    else if(*s >= 'A' && *s <= 'Z')
      d = *s - 'A' + 10;
    else
      break;
    if(d >= base)
      break;
    n = n * base + d;
    s++;
  }
  if(endptr)
    *endptr = (char*)(s == start ? orig : s);
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

static void (*_atexit_fns[32])(void);
static int _atexit_count;

static void
auxv6_run_atexit_handlers(void)
{
  while(_atexit_count > 0)
    _atexit_fns[--_atexit_count]();
}

int
atexit(void (*func)(void))
{
  if(_atexit_count >= 32)
    return -1;
  _atexit_fns[_atexit_count++] = func;
  return 0;
}

void
_Exit(int status)
{
  __auxv6_sys_exit(status);
  __builtin_unreachable();
}

void
exit(int status)
{
  auxv6_run_atexit_handlers();
  _Exit(status);
}

int
at_quick_exit(void (*func)(void))
{
  return atexit(func);
}

void
quick_exit(int status)
{
  auxv6_run_atexit_handlers();
  _Exit(status);
}

void
abort(void)
{
  kill(getpid(), 6);
  _Exit(EXIT_FAILURE);
  __builtin_unreachable();
}

void*
bsearch(const void *key, const void *base, uint nmemb, uint size,
        int (*compar)(const void*, const void*))
{
  uint lo;
  uint hi;

  lo = 0;
  hi = nmemb;
  while(lo < hi) {
    uint mid;
    const void *el;
    int cmp;

    mid = lo + (hi - lo) / 2;
    el = (const char*)base + mid * size;
    cmp = compar(key, el);
    if(cmp == 0)
      return (void*)el;
    if(cmp < 0)
      hi = mid;
    else
      lo = mid + 1;
  }
  return 0;
}

static void
_swap(char *a, char *b, uint n)
{
  char t;

  while(n--) {
    t = *a;
    *a++ = *b;
    *b++ = t;
  }
}

void
qsort(void *base, uint nmemb, uint size,
      int (*compar)(const void*, const void*))
{
  uint i;
  uint j;

  if(nmemb <= 1)
    return;
  for(i = 1; i < nmemb; i++)
    for(j = i; j > 0; j--) {
      char *a;
      char *b;

      a = (char*)base + (j - 1) * size;
      b = (char*)base + j * size;
      if(compar(a, b) > 0)
        _swap(a, b, size);
      else
        break;
    }
}

static unsigned int _rand_seed = 1;
static char *_rand_state;

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

int __attribute__((weak))
rand_r(unsigned int *seedp)
{
  if(seedp == 0)
    return rand();
  *seedp = *seedp * 1103515245u + 12345u;
  return (int)((*seedp >> 16) & 0x7fff);
}

long __attribute__((weak))
random(void)
{
  return (long)rand();
}

void __attribute__((weak))
srandom(unsigned int seed)
{
  srand(seed);
}

char* __attribute__((weak))
initstate(unsigned int seed, char *state, size_t n)
{
  char *old;

  old = _rand_state;
  if(state == 0 || n < sizeof(unsigned int)) {
    errno = EINVAL;
    return 0;
  }
  _rand_state = state;
  memmove(_rand_state, &seed, sizeof(seed));
  _rand_seed = seed;
  return old ? old : state;
}

char* __attribute__((weak))
setstate(char *state)
{
  char *old;

  if(state == 0) {
    errno = EINVAL;
    return 0;
  }
  old = _rand_state;
  _rand_state = state;
  memmove(&_rand_seed, _rand_state, sizeof(_rand_seed));
  return old ? old : state;
}

div_t
div(int numer, int denom)
{
  div_t out;

  out.quot = numer / denom;
  out.rem = numer % denom;
  return out;
}

ldiv_t
ldiv(long numer, long denom)
{
  ldiv_t out;

  out.quot = numer / denom;
  out.rem = numer % denom;
  return out;
}

lldiv_t
lldiv(long long numer, long long denom)
{
  lldiv_t out;

  out.quot = numer / denom;
  out.rem = numer % denom;
  return out;
}

int
mblen(const char *s, size_t n)
{
  if(s == 0)
    return 0;
  if(n == 0)
    return -1;
  return *s == '\0' ? 0 : 1;
}

int
mbtowc(wchar_t *pwc, const char *s, size_t n)
{
  if(s == 0)
    return 0;
  if(n == 0)
    return -1;
  if(*s == '\0') {
    if(pwc)
      *pwc = 0;
    return 0;
  }
  if(pwc)
    *pwc = (unsigned char)*s;
  return 1;
}

int
wctomb(char *s, wchar_t wchar)
{
  if(s == 0)
    return 0;
  if((unsigned long)wchar > 0xffUL) {
    errno = EILSEQ;
    return -1;
  }
  *s = (char)wchar;
  return 1;
}

size_t
mbstowcs(wchar_t *dest, const char *src, size_t n)
{
  size_t i;

  if(src == 0)
    return 0;
  for(i = 0; src[i] != '\0'; i++) {
    if(dest && i < n)
      dest[i] = (unsigned char)src[i];
  }
  if(dest && i < n)
    dest[i] = 0;
  return i;
}

size_t
wcstombs(char *dest, const wchar_t *src, size_t n)
{
  size_t i;

  if(src == 0)
    return 0;
  for(i = 0; src[i] != 0; i++) {
    if((unsigned long)src[i] > 0xffUL) {
      errno = EILSEQ;
      return (size_t)-1;
    }
    if(dest && i < n)
      dest[i] = (char)src[i];
  }
  if(dest && i < n)
    dest[i] = '\0';
  return i;
}