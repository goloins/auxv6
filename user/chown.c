#include "types.h"
#include "pwd.h"
#include "grp.h"
#include "sys/stat.h"
#include "fcntl.h"
#include "auxv6/user.h"

#define CHOWN_NAME_BUFSZ 32

static int
parse_decimal(const char *s)
{
  int value;

  if(s == 0 || *s == 0)
    return -1;
  value = 0;
  while(*s){
    if(*s < '0' || *s > '9')
      return -1;
    value = value * 10 + (*s - '0');
    s++;
  }
  return value;
}

int
main(int argc, char *argv[])
{
  int i;
  int uid;
  int gid;
  char *spec;
  char *colon;
  char ownerbuf[CHOWN_NAME_BUFSZ];
  int ownerlen;
  struct passwd *pw;
  struct group *gr;

  if(argc < 3){
    dprintf(2, "usage: chown [owner][:group] file...\n");
    exit(0);
  }

  spec = argv[1];
  colon = strchr(spec, ':');
  uid = -1;
  gid = -1;

  if(colon == 0) {
    /* owner only — change uid, leave gid untouched */
    uid = parse_decimal(spec);
    if(uid < 0) {
      pw = getpwnam(spec);
      if(pw == 0) {
        dprintf(2, "chown: unknown user %s\n", spec);
        exit(0);
      }
      uid = (int)pw->pw_uid;
    }
  } else {
    /* [owner]:group */
    ownerlen = (int)(colon - spec);
    if(ownerlen > 0) {
      if(ownerlen >= CHOWN_NAME_BUFSZ)
        ownerlen = CHOWN_NAME_BUFSZ - 1;
      memmove(ownerbuf, spec, ownerlen);
      ownerbuf[ownerlen] = 0;
      uid = parse_decimal(ownerbuf);
      if(uid < 0) {
        pw = getpwnam(ownerbuf);
        if(pw == 0) {
          dprintf(2, "chown: unknown user %s\n", ownerbuf);
          exit(0);
        }
        uid = (int)pw->pw_uid;
      }
    }
    if(colon[1] != 0) {
      gid = parse_decimal(colon + 1);
      if(gid < 0) {
        gr = getgrnam(colon + 1);
        if(gr == 0) {
          dprintf(2, "chown: unknown group %s\n", colon + 1);
          exit(0);
        }
        gid = (int)gr->gr_gid;
      }
    }
  }

  if(uid < 0 && gid < 0) {
    dprintf(2, "chown: no owner or group specified\n");
    exit(0);
  }

  for(i = 2; i < argc; i++){
    if(chown(argv[i], uid, gid) < 0){
      dprintf(2, "chown: %s failed\n", argv[i]);
      break;
    }
  }

  exit(0);
}