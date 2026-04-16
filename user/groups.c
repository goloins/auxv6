#include "types.h"
#include "pwd.h"
#include "sys/stat.h"
#include "auxv6/user.h"
#include "stdio.h"
#include "accountdb.h"

static char pbuf[ADB_FILE_MAX];
static char gbuf[ADB_FILE_MAX];

static int
print_groups_for_user(const char *user)
{
  int pn;
  int gn;
  int i;
  int first;
  struct adb_passwd_entry pw;

  if(adb_read_file("/etc/passwd", pbuf, sizeof(pbuf), &pn) < 0 ||
     adb_read_file("/etc/group", gbuf, sizeof(gbuf), &gn) < 0) {
    dprintf(2, "groups: cannot read account databases\n");
    return -1;
  }

  if(adb_find_user_by_name(pbuf, pn, user, &pw) < 0) {
    dprintf(2, "groups: unknown user %s\n", user);
    return -1;
  }

  dprintf(1, "%s :", user);
  first = 1;
  i = 0;
  while(i < gn) {
    int s;
    int e;
    struct adb_group_entry gr;
    int match;

    while(i < gn && (gbuf[i] == '\n' || gbuf[i] == '\r'))
      i++;
    if(i >= gn)
      break;
    s = i;
    while(i < gn && gbuf[i] != '\n' && gbuf[i] != '\r')
      i++;
    e = i;

    if(adb_parse_group_line(gbuf + s, e - s, &gr) < 0)
      continue;

    match = (gr.gid == pw.gid) || adb_group_has_member(&gr, user);
    if(!match)
      continue;
    dprintf(1, "%s%s", first ? " " : " ", gr.name);
    first = 0;
  }
  dprintf(1, "\n");
  return 0;
}

int
main(int argc, char *argv[])
{
  int i;

  if(argc == 1) {
    struct passwd *pw;

    pw = getpwuid(getuid());
    if(pw == 0) {
      dprintf(2, "groups: cannot resolve current user\n");
      exit(0);
    }
    if(print_groups_for_user(pw->pw_name) < 0)
      exit(0);
    exit(0);
  }

  for(i = 1; i < argc; i++) {
    if(print_groups_for_user(argv[i]) < 0)
      break;
  }

  exit(0);
}
