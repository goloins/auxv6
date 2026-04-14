#include "types.h"
#include "sys/stat.h"
#include "auxv6/user.h"
#include "stdio.h"
#include "accountdb.h"

static char pbuf[ADB_FILE_MAX];
static char pout[ADB_FILE_MAX];
static char gbuf[ADB_FILE_MAX];
static char gout[ADB_FILE_MAX];
static struct adb_group_entry target;

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
  int pn;
  int gn;
  int i;
  char *oldname;
  char *newname;
  int newgid;
  int set_gid;
  int goutn;
  int poutn;

  if(getuid() != 0) {
    dprintf(2, "groupmod: must be root\n");
    exit(0);
  }

  oldname = 0;
  newname = 0;
  newgid = -1;
  set_gid = 0;

  for(i = 1; i < argc; i++) {
    if(strcmp(argv[i], "-n") == 0) {
      if(i + 1 >= argc) {
        dprintf(2, "usage: groupmod [-n newname] [-g gid] group\n");
        exit(0);
      }
      newname = argv[++i];
      continue;
    }
    if(strcmp(argv[i], "-g") == 0) {
      if(i + 1 >= argc || parse_int(argv[i + 1], &newgid) < 0) {
        dprintf(2, "usage: groupmod [-n newname] [-g gid] group\n");
        exit(0);
      }
      set_gid = 1;
      i++;
      continue;
    }
    if(argv[i][0] == '-') {
      dprintf(2, "usage: groupmod [-n newname] [-g gid] group\n");
      exit(0);
    }
    oldname = argv[i];
  }

  if(oldname == 0 || (!newname && !set_gid)) {
    dprintf(2, "usage: groupmod [-n newname] [-g gid] group\n");
    exit(0);
  }

  if(newname && !adb_is_valid_name(newname)) {
    dprintf(2, "groupmod: invalid new group name\n");
    exit(0);
  }

  if(adb_read_file("/etc/group", gbuf, sizeof(gbuf), &gn) < 0 ||
     adb_read_file("/etc/passwd", pbuf, sizeof(pbuf), &pn) < 0) {
    dprintf(2, "groupmod: cannot read account databases\n");
    exit(0);
  }

  if(adb_find_group_by_name(gbuf, gn, oldname, &target) < 0) {
    dprintf(2, "groupmod: unknown group %s\n", oldname);
    exit(0);
  }

  if(newname && strcmp(newname, oldname) != 0 && adb_find_group_by_name(gbuf, gn, newname, 0) == 0) {
    dprintf(2, "groupmod: group exists: %s\n", newname);
    exit(0);
  }

  if(set_gid && newgid != target.gid && adb_find_group_by_gid(gbuf, gn, newgid, 0) == 0) {
    dprintf(2, "groupmod: gid exists: %d\n", newgid);
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

    if(adb_parse_group_line(gbuf + s, e - s, &gr) == 0 && strcmp(gr.name, oldname) == 0) {
      if(newname)
        snprintf(gr.name, sizeof(gr.name), "%s", newname);
      if(set_gid)
        gr.gid = newgid;
      if(adb_append_group_line(gout, &goutn, sizeof(gout), &gr) < 0) {
        dprintf(2, "groupmod: output overflow\n");
        exit(0);
      }
      continue;
    }

    if(adb_append_raw_line(gout, &goutn, sizeof(gout), gbuf + s, e - s) < 0) {
      dprintf(2, "groupmod: output overflow\n");
      exit(0);
    }
  }

  if(set_gid && newgid != target.gid) {
    poutn = 0;
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

      if(adb_parse_passwd_line(pbuf + s, e - s, &pw) == 0) {
        if(pw.gid == target.gid)
          pw.gid = newgid;
        if(adb_append_passwd_line(pout, &poutn, sizeof(pout), &pw) < 0) {
          dprintf(2, "groupmod: passwd output overflow\n");
          exit(0);
        }
      } else {
        if(adb_append_raw_line(pout, &poutn, sizeof(pout), pbuf + s, e - s) < 0) {
          dprintf(2, "groupmod: passwd output overflow\n");
          exit(0);
        }
      }
    }

    if(adb_write_file_atomic("/etc/passwd", "/etc/passwd.tmp", pout, poutn) < 0) {
      dprintf(2, "groupmod: failed to update /etc/passwd\n");
      exit(0);
    }
  }

  if(adb_write_file_atomic("/etc/group", "/etc/group.tmp", gout, goutn) < 0) {
    dprintf(2, "groupmod: failed to update /etc/group\n");
    exit(0);
  }

  dprintf(1, "groupmod: updated %s\n", oldname);
  exit(0);
}
