#include "types.h"
#include "sys/stat.h"
#include "auxv6/user.h"
#include "stdio.h"
#include "accountdb.h"

static char gbuf[ADB_FILE_MAX];
static struct adb_group_entry gr;

static int
parse_int(const char *s, int *out)
{
  int i;
  int v;

  if(s == 0 || *s == 0 || out == 0)
    return -1;
  v = 0;
  for(i = 0; s[i]; i++) {
    if(s[i] < '0' || s[i] > '9')
      return -1;
    v = v * 10 + (s[i] - '0');
  }
  *out = v;
  return 0;
}

int
main(int argc, char *argv[])
{
  int gn;
  int gid;
  int outn;
  int i;
  int need_gid;
  char *name;

  if(getuid() != 0) {
    dprintf(2, "groupadd: must be root\n");
    exit(0);
  }

  gid = -1;
  need_gid = 0;
  name = 0;

  for(i = 1; i < argc; i++) {
    if(strcmp(argv[i], "-g") == 0) {
      if(i + 1 >= argc || parse_int(argv[i + 1], &gid) < 0) {
        dprintf(2, "usage: groupadd [-g gid] group\n");
        exit(0);
      }
      need_gid = 1;
      i++;
      continue;
    }
    if(argv[i][0] == '-') {
      dprintf(2, "usage: groupadd [-g gid] group\n");
      exit(0);
    }
    name = argv[i];
  }

  if(name == 0 || !adb_is_valid_name(name)) {
    dprintf(2, "groupadd: invalid group name\n");
    exit(0);
  }

  if(adb_read_file("/etc/group", gbuf, sizeof(gbuf), &gn) < 0) {
    dprintf(2, "groupadd: cannot read /etc/group\n");
    exit(0);
  }

  if(adb_find_group_by_name(gbuf, gn, name, 0) == 0) {
    dprintf(2, "groupadd: group exists: %s\n", name);
    exit(0);
  }

  if(!need_gid)
    gid = adb_next_gid(gbuf, gn, 1000);

  if(gid < 0 || adb_find_group_by_gid(gbuf, gn, gid, 0) == 0) {
    dprintf(2, "groupadd: gid already exists: %d\n", gid);
    exit(0);
  }

  memset(&gr, 0, sizeof(gr));
  snprintf(gr.name, sizeof(gr.name), "%s", name);
  snprintf(gr.passwd, sizeof(gr.passwd), "x");
  gr.gid = gid;
  gr.members[0] = 0;

  outn = 0;
  if(gn >= (int)sizeof(gbuf) - 2) {
    dprintf(2, "groupadd: group database too large\n");
    exit(0);
  }
  if(gn > 0) {
    outn = gn;
    if(gbuf[outn - 1] != '\n')
      gbuf[outn++] = '\n';
  }
  if(adb_append_group_line(gbuf, &outn, sizeof(gbuf), &gr) < 0) {
    dprintf(2, "groupadd: failed to append group\n");
    exit(0);
  }

  if(adb_write_file_atomic("/etc/group", "/etc/group.tmp", gbuf, outn) < 0) {
    dprintf(2, "groupadd: failed to write /etc/group\n");
    exit(0);
  }

  dprintf(1, "groupadd: added %s (gid=%d)\n", gr.name, gr.gid);
  exit(0);
}
