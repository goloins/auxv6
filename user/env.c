/*
 * env.c - environment helpers split out of user/ulib.c
 */

#include "types.h"
#include "auxv6/user.h"

extern char **environ;

static uint
_envcount(char **env)
{
  uint n;

  n = 0;
  if(env)
    while(env[n])
      n++;
  return n;
}

char*
getenv(const char *name)
{
  uint len;
  char **ep;

  len = strlen(name);
  if(!environ)
    return 0;
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

  for(len = 0; string[len] && string[len] != '='; len++)
    ;
  n = _envcount(environ);
  i = 0;
  for(i = 0; i < n; i++) {
    if(strncmp(environ[i], string, len) == 0 && environ[i][len] == '=') {
      environ[i] = string;
      return 0;
    }
  }
  {
    char **nenv;

    nenv = malloc((n + 2) * sizeof(char*));
    if(!nenv)
      return -1;
    for(i = 0; i < n; i++)
      nenv[i] = environ[i];
    nenv[n] = string;
    nenv[n + 1] = 0;
    environ = nenv;
  }
  return 0;
}

int
setenv(const char *name, const char *value, int overwrite)
{
  uint nlen;
  uint vlen;
  char **ep;
  uint i;

  nlen = strlen(name);
  vlen = strlen(value);
  i = 0;
  if(environ) {
    for(ep = environ; *ep; ep++, i++) {
      if(strncmp(*ep, name, nlen) == 0 && (*ep)[nlen] == '=') {
        if(!overwrite)
          return 0;
        {
          char *slot;

          slot = malloc(nlen + 1 + vlen + 1);
          if(!slot)
            return -1;
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
    char *entry;

    entry = malloc(nlen + 1 + vlen + 1);
    if(!entry)
      return -1;
    strcpy(entry, name);
    entry[nlen] = '=';
    strcpy(entry + nlen + 1, value);
    return putenv(entry);
  }
}

int
unsetenv(const char *name)
{
  uint len;
  uint n;
  uint i;

  len = strlen(name);
  n = _envcount(environ);
  for(i = 0; i < n; i++) {
    if(strncmp(environ[i], name, len) == 0 && environ[i][len] == '=') {
      for(; i < n; i++)
        environ[i] = environ[i + 1];
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