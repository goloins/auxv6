#include "../include/types.h"
#include "../include/stat.h"
#include "../include/user.h"
#include "../include/fcntl.h"
#include "../include/fs.h"

int
rm_path(char *path, int recursive)
{
  int fd;
  int ok;
  char buf[512];
  char name[DIRSIZ + 1];
  char *p;
  struct stat st;
  struct dirent de;

  if(path == 0 || path[0] == 0)
    return -1;
  if(strcmp(path, ".") == 0 || strcmp(path, "..") == 0)
    return -1;

  if(stat(path, &st) < 0)
    return -1;

  if(st.st_type != T_DIR)
    return unlink(path);

  if(!recursive)
    return -2;

  fd = open(path, O_RDONLY);
  if(fd < 0)
    return -1;

  ok = 0;
  while(read(fd, &de, sizeof(de)) == sizeof(de)) {
    if(de.inum == 0)
      continue;

    memmove(name, de.name, DIRSIZ);
    name[DIRSIZ] = 0;

    if(strcmp(name, ".") == 0 || strcmp(name, "..") == 0)
      continue;

    if(strlen(path) + 1 + DIRSIZ + 1 > sizeof(buf)) {
      ok = -1;
      break;
    }

    strcpy(buf, path);
    p = buf + strlen(buf);
    if(p > buf && p[-1] != '/')
      *p++ = '/';
    strcpy(p, name);

    if(rm_path(buf, 1) < 0)
      ok = -1;
  }
  close(fd);

  if(ok < 0)
    return -1;

  return unlink(path);
}

int
main(int argc, char *argv[])
{
  int i;
  int recursive;
  int ret;

  recursive = 0;

  if(argc < 2){
    printf(2, "Usage: rm [-r] files...\n");
    exit();
  }

  for(i = 1; i < argc; i++) {
    if(argv[i][0] == '-') {
      if(strcmp(argv[i], "-r") == 0)
        recursive = 1;
      else {
        printf(2, "rm: unknown option %s\n", argv[i]);
        exit();
      }
      continue;
    }

    ret = rm_path(argv[i], recursive);
    if(ret == -2) {
      printf(2, "rm: %s is a directory (use -r)\n", argv[i]);
      break;
    }
    if(ret < 0){
      printf(2, "rm: %s failed to delete\n", argv[i]);
      break;
    }
  }

  exit();
}
