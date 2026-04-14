#include "types.h"
#include "stat.h"
#include "auxv6/user.h"
#include "stdio.h"
#include "accountdb.h"

static char pbuf[ADB_FILE_MAX];
static char gbuf[ADB_FILE_MAX];
static char gout[ADB_FILE_MAX];
static struct adb_group_entry victim;

int
main(int argc, char *argv[])
{
  int pn;
  int gn;
  int outn;
  int i;
  int removed;
  char *name;

  if(getuid() != 0) {
    dprintf(2, "groupdel: must be root\n");
    exit(0);
  }

  if(argc != 2) {
    dprintf(2, "usage: groupdel group\n");
    exit(0);
  }
  name = argv[1];

  if(adb_read_file("/etc/group", gbuf, sizeof(gbuf), &gn) < 0 ||
     adb_read_file("/etc/passwd", pbuf, sizeof(pbuf), &pn) < 0) {
    dprintf(2, "groupdel: cannot read account databases\n");
    exit(0);
  }

  if(adb_find_group_by_name(gbuf, gn, name, &victim) < 0) {
    dprintf(2, "groupdel: unknown group %s\n", name);
    exit(0);
  }

  i = 0;
  while(i < pn) {
    int s;
    int e;
    struct adb_passwd_entry pw;

    while(i < pn && (pbuf[i] == '\n' || pbuf[i] == '\r'))
      i++;
    if(i >= pn)
      break;
    if(pbuf[i] == '#') {
      while(i < pn && pbuf[i] != '\n' && pbuf[i] != '\r')
        i++;
      continue;
    }

    s = i;
    while(i < pn && pbuf[i] != '\n' && pbuf[i] != '\r')
      i++;
    e = i;

    if(adb_parse_passwd_line(pbuf + s, e - s, &pw) == 0 && pw.gid == victim.gid) {
      dprintf(2, "groupdel: group %s is primary for user %s\n", name, pw.name);
      exit(0);
    }
  }

  outn = 0;
  removed = 0;
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

    if(adb_parse_group_line(gbuf + s, e - s, &gr) == 0 && strcmp(gr.name, name) == 0) {
      removed = 1;
      continue;
    }
    if(adb_append_raw_line(gout, &outn, sizeof(gout), gbuf + s, e - s) < 0) {
      dprintf(2, "groupdel: output buffer overflow\n");
      exit(0);
    }
  }

  if(!removed) {
    dprintf(2, "groupdel: unknown group %s\n", name);
    exit(0);
  }

  if(adb_write_file_atomic("/etc/group", "/etc/group.tmp", gout, outn) < 0) {
    dprintf(2, "groupdel: failed to write /etc/group\n");
    exit(0);
  }

  dprintf(1, "groupdel: removed %s\n", name);
  exit(0);
}
