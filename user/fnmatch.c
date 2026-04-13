#include "types.h"
#include "fnmatch.h"
#include "ctype.h"
#include "string.h"

static int
ch_eq(char a, char b, int flags)
{
  if(flags & FNM_CASEFOLD) {
    return tolower((uchar)a) == tolower((uchar)b);
  }
  return a == b;
}

static int
is_leading_period(const char *s, const char *start, int flags)
{
  if(!(flags & FNM_PERIOD))
    return 0;
  if(*s != '.')
    return 0;
  if(s == start)
    return 1;
  if((flags & FNM_PATHNAME) && s[-1] == '/')
    return 1;
  return 0;
}

static int
match_class(const char **pp, char c, int flags)
{
  const char *p;
  int negate;
  int matched;
  int first;

  p = *pp;
  negate = 0;
  matched = 0;

  if(*p == '!' || *p == '^') {
    negate = 1;
    p++;
  }

  first = 1;
  while(*p && *p != ']') {
    char start;
    char end;

    if(*p == '\\' && !(flags & FNM_NOESCAPE) && p[1])
      p++;
    start = *p++;

    if(!first && start == ']')
      break;

    if(*p == '-' && p[1] && p[1] != ']') {
      p++;
      if(*p == '\\' && !(flags & FNM_NOESCAPE) && p[1])
        p++;
      end = *p++;
      if(flags & FNM_CASEFOLD) {
        char lc = (char)tolower((uchar)c);
        char ls = (char)tolower((uchar)start);
        char le = (char)tolower((uchar)end);
        if(ls <= lc && lc <= le)
          matched = 1;
      } else {
        if(start <= c && c <= end)
          matched = 1;
      }
    } else if(ch_eq(start, c, flags)) {
      matched = 1;
    }

    first = 0;
  }

  if(*p != ']')
    return -1;

  *pp = p + 1;
  if(negate)
    return matched ? 0 : 1;
  return matched ? 1 : 0;
}

static int
match_here(const char *pat, const char *str, const char *str0, int flags)
{
  while(*pat) {
    if(*pat == '*') {
      while(*pat == '*')
        pat++;

      if(is_leading_period(str, str0, flags))
        return FNM_NOMATCH;

      if(*pat == '\0') {
        if((flags & FNM_PATHNAME) && strchr(str, '/'))
          return FNM_NOMATCH;
        return 0;
      }

      for(;;) {
        if(!(flags & FNM_PATHNAME) || *str != '/') {
          if(match_here(pat, str, str0, flags) == 0)
            return 0;
        }
        if(*str == '\0')
          break;
        if((flags & FNM_PATHNAME) && *str == '/')
          break;
        str++;
      }
      return FNM_NOMATCH;
    }

    if(*str == '\0')
      return FNM_NOMATCH;

    if(*pat == '?') {
      if((flags & FNM_PATHNAME) && *str == '/')
        return FNM_NOMATCH;
      if(is_leading_period(str, str0, flags))
        return FNM_NOMATCH;
      pat++;
      str++;
      continue;
    }

    if(*pat == '[') {
      int ok;
      const char *pnext;

      if((flags & FNM_PATHNAME) && *str == '/')
        return FNM_NOMATCH;
      if(is_leading_period(str, str0, flags))
        return FNM_NOMATCH;

      pnext = pat + 1;
      ok = match_class(&pnext, *str, flags);
      if(ok < 0) {
        if(!ch_eq(*pat, *str, flags))
          return FNM_NOMATCH;
        pat++;
        str++;
        continue;
      }
      if(!ok)
        return FNM_NOMATCH;
      pat = pnext;
      str++;
      continue;
    }

    if(*pat == '\\' && !(flags & FNM_NOESCAPE) && pat[1])
      pat++;

    if(!ch_eq(*pat, *str, flags))
      return FNM_NOMATCH;

    pat++;
    str++;
  }

  return *str == '\0' ? 0 : FNM_NOMATCH;
}

int
fnmatch(const char *pattern, const char *string, int flags)
{
  if(pattern == 0 || string == 0)
    return FNM_NOMATCH;
  return match_here(pattern, string, string, flags);
}