#include "types.h"
#include "pwd.h"
#include "grp.h"
#include "stat.h"
#include "stdio.h"
#include "auxv6/user.h"
#include "fcntl.h"

#define NAME_MAX 32

int
main(void)
{
  int uid;
  int gid;
  char user[NAME_MAX];
  char grpname[NAME_MAX];
  struct passwd *pw;
  struct group *gr;

  uid = getuid();
  gid = getgid();

  pw = getpwuid((uid_t)uid);
  if(pw == 0)
    snprintf(user, sizeof(user), "%d", uid);
  else
    snprintf(user, sizeof(user), "%s", pw->pw_name);

  gr = getgrgid((gid_t)gid);
  if(gr == 0)
    snprintf(grpname, sizeof(grpname), "%d", gid);
  else
    snprintf(grpname, sizeof(grpname), "%s", gr->gr_name);

  dprintf(1, "uid=%d(%s) gid=%d(%s)\n", uid, user, gid, grpname);
  exit(0);
}
