#include "types.h"
#include "stat.h"
#include "auxv6/user.h"

char *getenv(const char *name);

static int
is_executable_file(const char *path)
{
  struct stat st;

  if(stat(path, &st) < 0)
    return 0;
  if(st.st_type != T_FILE)
    return 0;
  return (st.st_mode & (M_IXUSR | M_IXGRP | M_IXOTH)) != 0;
}

static int
has_slash(const char *s)
{
  int i;

  for(i = 0; s[i]; i++)
    if(s[i] == '/')
      return 1;
  return 0;
}

static int
search_path(const char *name, const char *path)
{
  char full[256];
  int i;

  i = 0;
  while(1){
    int start = i;
    int dlen;
    int j;
    int k;

    while(path[i] && path[i] != ':')
      i++;
    dlen = i - start;

    if(dlen == 0){
      j = 0;
      full[j++] = '.';
    } else {
      if(dlen >= (int)sizeof(full) - 2)
        goto next;
      for(j = 0; j < dlen; j++)
        full[j] = path[start + j];
    }

    if(j == 0 || full[j - 1] != '/')
      full[j++] = '/';

    for(k = 0; name[k] && j < (int)sizeof(full) - 1; k++, j++)
      full[j] = name[k];
    full[j] = 0;

    if(name[k] == 0 && is_executable_file(full)){
      printf(1, "%s\n", full);
      return 1;
    }

  next:
    if(path[i] == 0)
      break;
    i++;
  }

  return 0;
}

int
main(int argc, char *argv[])
{
  int i;
  int found_all;
  char *path;

  if(argc < 2){
    printf(2, "usage: which name...\n");
    exit();
  }

  path = getenv("PATH");
  if(path == 0 || path[0] == 0)
    path = "/:/bin:/sbin";

  found_all = 1;
  for(i = 1; i < argc; i++){
    if(has_slash(argv[i])){
      if(is_executable_file(argv[i]))
        printf(1, "%s\n", argv[i]);
      else {
        printf(2, "which: %s not found\n", argv[i]);
        found_all = 0;
      }
      continue;
    }

    if(!search_path(argv[i], path)){
      printf(2, "which: %s not found\n", argv[i]);
      found_all = 0;
    }
  }

  if(!found_all)
    exit();
  exit();
}
