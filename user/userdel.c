#include "types.h"
#include "sys/stat.h"
#include "auxv6/user.h"
#include "stdio.h"
#include "accountdb.h"

static char pbuf[ADB_FILE_MAX];
static char gbuf[ADB_FILE_MAX];
static char pout[ADB_FILE_MAX];
static char gout[ADB_FILE_MAX];
static struct adb_passwd_entry victim;

int
main(int argc, char *argv[])
{
  int pn;
  int gn;
  int poutn;
  int goutn;
  int i;
  int removed;
  char *name;

  if(getuid() != 0) {
    dprintf(2, "userdel: must be root\n");
    exit(0);
  }

  if(argc != 2) {
    dprintf(2, "usage: userdel user\n");
    exit(0);
  }
  name = argv[1];

  if(adb_read_file("/etc/passwd", pbuf, sizeof(pbuf), &pn) < 0 ||
     adb_read_file("/etc/group", gbuf, sizeof(gbuf), &gn) < 0) {
    dprintf(2, "userdel: cannot read account databases\n");
    exit(0);
  }

  if(adb_find_user_by_name(pbuf, pn, name, &victim) < 0) {
    dprintf(2, "userdel: unknown user %s\n", name);
    exit(0);
  }
  if(victim.uid == 0) {
    dprintf(2, "userdel: refusing to delete root\n");
    exit(0);
  }

  poutn = 0;
  removed = 0;
  i = 0;
  while(i < pn) {
    int s;
    int e;
    struct adb_passwd_entry pw;

    while(i < pn && (pbuf[i] == '\n' || pbuf[i] == '\r'))
      i++;
    if(i >= pn)
      break;
    s = i;
    while(i < pn && pbuf[i] != '\n' && pbuf[i] != '\r')
      i++;
    e = i;

    if(adb_parse_passwd_line(pbuf + s, e - s, &pw) == 0 && strcmp(pw.name, name) == 0) {
      removed = 1;
      continue;
    }
    if(adb_append_raw_line(pout, &poutn, sizeof(pout), pbuf + s, e - s) < 0) {
      dprintf(2, "userdel: output overflow\n");
      exit(0);
    }
  }

  if(!removed) {
    dprintf(2, "userdel: unknown user %s\n", name);
    exit(0);
  }

  goutn = 0;
  i = 0;
  while(i < gn) {
    int s;
    int e;
    struct adb_group_entry gr;

    while(i < gn && (gbuf[i] == '\n' || gbuf[i] == '\r'))
      i++;
    if(i >= gn)
      break;
    s = i;
    while(i < gn && gbuf[i] != '\n' && gbuf[i] != '\r')
      i++;
    e = i;

    if(adb_parse_group_line(gbuf + s, e - s, &gr) == 0) {
      adb_group_remove_member(&gr, name);
      if(adb_append_group_line(gout, &goutn, sizeof(gout), &gr) < 0) {
        dprintf(2, "userdel: group output overflow\n");
        exit(0);
      }
    } else {
      if(adb_append_raw_line(gout, &goutn, sizeof(gout), gbuf + s, e - s) < 0) {
        dprintf(2, "userdel: group output overflow\n");
        exit(0);
      }
    }
  }

  if(adb_write_file_atomic("/etc/group", "/etc/group.tmp", gout, goutn) < 0 ||
     adb_write_file_atomic("/etc/passwd", "/etc/passwd.tmp", pout, poutn) < 0) {
    dprintf(2, "userdel: failed to update account databases\n");
    exit(0);
  }

  dprintf(1, "userdel: removed %s\n", name);
  exit(0);
}
