#include "types.h"
#include "sys/stat.h"
#include "auxv6/user.h"
#include "stdio.h"
#include "accountdb.h"

static char pbuf[ADB_FILE_MAX];
static char gbuf[ADB_FILE_MAX];
static char pout[ADB_FILE_MAX];
static char gout[ADB_FILE_MAX];
static struct adb_passwd_entry target;

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

static void
rewrite_group_member_name(struct adb_group_entry *gr, const char *oldname, const char *newname)
{
  if(adb_group_remove_member(gr, oldname) == 0)
    adb_group_add_member(gr, newname);
}

int
main(int argc, char *argv[])
{
  int pn;
  int gn;
  int poutn;
  int goutn;
  int i;
  char *name;
  char *newname;
  char *home;
  char *shell;
  char *gecos;
  int set_uid;
  int set_gid;
  int newuid;
  int newgid;

  if(getuid() != 0) {
    dprintf(2, "usermod: must be root\n");
    exit(0);
  }

  name = 0;
  newname = 0;
  home = 0;
  shell = 0;
  gecos = 0;
  set_uid = 0;
  set_gid = 0;
  newuid = -1;
  newgid = -1;

  for(i = 1; i < argc; i++) {
    if(strcmp(argv[i], "-l") == 0) {
      if(i + 1 >= argc) {
        dprintf(2, "usage: usermod [-l login] [-u uid] [-g group|gid] [-d home] [-s shell] [-c gecos] user\n");
        exit(0);
      }
      newname = argv[++i];
      continue;
    }
    if(strcmp(argv[i], "-u") == 0) {
      if(i + 1 >= argc || parse_int(argv[i + 1], &newuid) < 0) {
        dprintf(2, "usage: usermod [-l login] [-u uid] [-g group|gid] [-d home] [-s shell] [-c gecos] user\n");
        exit(0);
      }
      set_uid = 1;
      i++;
      continue;
    }
    if(strcmp(argv[i], "-g") == 0) {
      if(i + 1 >= argc) {
        dprintf(2, "usage: usermod [-l login] [-u uid] [-g group|gid] [-d home] [-s shell] [-c gecos] user\n");
        exit(0);
      }
      if(parse_int(argv[i + 1], &newgid) < 0) {
        struct adb_group_entry gtmp;
        if(adb_read_file("/etc/group", gbuf, sizeof(gbuf), &gn) < 0 ||
           adb_find_group_by_name(gbuf, gn, argv[i + 1], &gtmp) < 0) {
          dprintf(2, "usermod: unknown group %s\n", argv[i + 1]);
          exit(0);
        }
        newgid = gtmp.gid;
      }
      set_gid = 1;
      i++;
      continue;
    }
    if(strcmp(argv[i], "-d") == 0) {
      if(i + 1 >= argc) {
        dprintf(2, "usage: usermod [-l login] [-u uid] [-g group|gid] [-d home] [-s shell] [-c gecos] user\n");
        exit(0);
      }
      home = argv[++i];
      continue;
    }
    if(strcmp(argv[i], "-s") == 0) {
      if(i + 1 >= argc) {
        dprintf(2, "usage: usermod [-l login] [-u uid] [-g group|gid] [-d home] [-s shell] [-c gecos] user\n");
        exit(0);
      }
      shell = argv[++i];
      continue;
    }
    if(strcmp(argv[i], "-c") == 0) {
      if(i + 1 >= argc) {
        dprintf(2, "usage: usermod [-l login] [-u uid] [-g group|gid] [-d home] [-s shell] [-c gecos] user\n");
        exit(0);
      }
      gecos = argv[++i];
      continue;
    }
    if(argv[i][0] == '-') {
      dprintf(2, "usage: usermod [-l login] [-u uid] [-g group|gid] [-d home] [-s shell] [-c gecos] user\n");
      exit(0);
    }
    name = argv[i];
  }

  if(name == 0 || (!newname && !set_uid && !set_gid && !home && !shell && !gecos)) {
    dprintf(2, "usage: usermod [-l login] [-u uid] [-g group|gid] [-d home] [-s shell] [-c gecos] user\n");
    exit(0);
  }

  if(newname && !adb_is_valid_name(newname)) {
    dprintf(2, "usermod: invalid login name\n");
    exit(0);
  }

  if(adb_read_file("/etc/passwd", pbuf, sizeof(pbuf), &pn) < 0 ||
     adb_read_file("/etc/group", gbuf, sizeof(gbuf), &gn) < 0) {
    dprintf(2, "usermod: cannot read account databases\n");
    exit(0);
  }

  if(adb_find_user_by_name(pbuf, pn, name, &target) < 0) {
    dprintf(2, "usermod: unknown user %s\n", name);
    exit(0);
  }

  if(newname && strcmp(name, newname) != 0 && adb_find_user_by_name(pbuf, pn, newname, 0) == 0) {
    dprintf(2, "usermod: user exists: %s\n", newname);
    exit(0);
  }

  if(set_uid && newuid != target.uid && adb_find_user_by_uid(pbuf, pn, newuid, 0) == 0) {
    dprintf(2, "usermod: uid exists: %d\n", newuid);
    exit(0);
  }

  if(set_gid && adb_find_group_by_gid(gbuf, gn, newgid, 0) < 0) {
    dprintf(2, "usermod: gid does not exist: %d\n", newgid);
    exit(0);
  }

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

    if(adb_parse_passwd_line(pbuf + s, e - s, &pw) == 0 && strcmp(pw.name, name) == 0) {
      if(newname)
        snprintf(pw.name, sizeof(pw.name), "%s", newname);
      if(set_uid)
        pw.uid = newuid;
      if(set_gid)
        pw.gid = newgid;
      if(home)
        snprintf(pw.home, sizeof(pw.home), "%s", home);
      if(shell)
        snprintf(pw.shell, sizeof(pw.shell), "%s", shell);
      if(gecos)
        snprintf(pw.gecos, sizeof(pw.gecos), "%s", gecos);
      if(adb_append_passwd_line(pout, &poutn, sizeof(pout), &pw) < 0) {
        dprintf(2, "usermod: output overflow\n");
        exit(0);
      }
      continue;
    }

    if(adb_append_raw_line(pout, &poutn, sizeof(pout), pbuf + s, e - s) < 0) {
      dprintf(2, "usermod: output overflow\n");
      exit(0);
    }
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
      if(newname)
        rewrite_group_member_name(&gr, name, newname);
      if(adb_append_group_line(gout, &goutn, sizeof(gout), &gr) < 0) {
        dprintf(2, "usermod: group output overflow\n");
        exit(0);
      }
    } else {
      if(adb_append_raw_line(gout, &goutn, sizeof(gout), gbuf + s, e - s) < 0) {
        dprintf(2, "usermod: group output overflow\n");
        exit(0);
      }
    }
  }

  if(adb_write_file_atomic("/etc/group", "/etc/group.tmp", gout, goutn) < 0 ||
     adb_write_file_atomic("/etc/passwd", "/etc/passwd.tmp", pout, poutn) < 0) {
    dprintf(2, "usermod: failed to update account databases\n");
    exit(0);
  }

  dprintf(1, "usermod: updated %s\n", name);
  exit(0);
}
