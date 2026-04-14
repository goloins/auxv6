#include "types.h"
#include "stat.h"
#include "auxv6/user.h"
#include "stdio.h"
#include "accountdb.h"

static char pbuf[ADB_FILE_MAX];
static char gbuf[ADB_FILE_MAX];
static char pout[ADB_FILE_MAX];
static char gout[ADB_FILE_MAX];
static struct adb_passwd_entry pw;
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
  int pn;
  int gn;
  int i;
  int uid;
  int gid;
  int set_uid;
  int set_gid;
  int no_home;
  int create_private_group;
  char *name;
  char *home;
  char *shell;
  char *gecos;
  int poutn;
  int goutn;

  if(getuid() != 0) {
    dprintf(2, "useradd: must be root\n");
    exit(0);
  }

  uid = -1;
  gid = -1;
  set_uid = 0;
  set_gid = 0;
  no_home = 0;
  create_private_group = 1;
  name = 0;
  home = 0;
  shell = "/bin/sh";
  gecos = "";

  for(i = 1; i < argc; i++) {
    if(strcmp(argv[i], "-u") == 0) {
      if(i + 1 >= argc || parse_int(argv[i + 1], &uid) < 0) {
        dprintf(2, "usage: useradd [-u uid] [-g group|gid] [-d home] [-s shell] [-c gecos] [-M] user\n");
        exit(0);
      }
      set_uid = 1;
      i++;
      continue;
    }
    if(strcmp(argv[i], "-g") == 0) {
      if(i + 1 >= argc) {
        dprintf(2, "usage: useradd [-u uid] [-g group|gid] [-d home] [-s shell] [-c gecos] [-M] user\n");
        exit(0);
      }
      if(parse_int(argv[i + 1], &gid) < 0) {
        struct adb_group_entry gtmp;
        if(adb_read_file("/etc/group", gbuf, sizeof(gbuf), &gn) < 0 ||
           adb_find_group_by_name(gbuf, gn, argv[i + 1], &gtmp) < 0) {
          dprintf(2, "useradd: unknown group %s\n", argv[i + 1]);
          exit(0);
        }
        gid = gtmp.gid;
      }
      set_gid = 1;
      create_private_group = 0;
      i++;
      continue;
    }
    if(strcmp(argv[i], "-d") == 0) {
      if(i + 1 >= argc) {
        dprintf(2, "usage: useradd [-u uid] [-g group|gid] [-d home] [-s shell] [-c gecos] [-M] user\n");
        exit(0);
      }
      home = argv[++i];
      continue;
    }
    if(strcmp(argv[i], "-s") == 0) {
      if(i + 1 >= argc) {
        dprintf(2, "usage: useradd [-u uid] [-g group|gid] [-d home] [-s shell] [-c gecos] [-M] user\n");
        exit(0);
      }
      shell = argv[++i];
      continue;
    }
    if(strcmp(argv[i], "-c") == 0) {
      if(i + 1 >= argc) {
        dprintf(2, "usage: useradd [-u uid] [-g group|gid] [-d home] [-s shell] [-c gecos] [-M] user\n");
        exit(0);
      }
      gecos = argv[++i];
      continue;
    }
    if(strcmp(argv[i], "-M") == 0) {
      no_home = 1;
      continue;
    }
    if(argv[i][0] == '-') {
      dprintf(2, "usage: useradd [-u uid] [-g group|gid] [-d home] [-s shell] [-c gecos] [-M] user\n");
      exit(0);
    }
    name = argv[i];
  }

  if(name == 0 || !adb_is_valid_name(name)) {
    dprintf(2, "useradd: invalid user name\n");
    exit(0);
  }

  if(adb_read_file("/etc/passwd", pbuf, sizeof(pbuf), &pn) < 0 ||
     adb_read_file("/etc/group", gbuf, sizeof(gbuf), &gn) < 0) {
    dprintf(2, "useradd: cannot read account databases\n");
    exit(0);
  }

  if(adb_find_user_by_name(pbuf, pn, name, 0) == 0) {
    dprintf(2, "useradd: user exists: %s\n", name);
    exit(0);
  }

  if(!set_uid)
    uid = adb_next_uid(pbuf, pn, 1000);
  if(uid < 0 || adb_find_user_by_uid(pbuf, pn, uid, 0) == 0) {
    dprintf(2, "useradd: uid exists: %d\n", uid);
    exit(0);
  }

  if(!set_gid) {
    if(adb_find_group_by_name(gbuf, gn, name, &gr) == 0) {
      gid = gr.gid;
      create_private_group = 0;
    } else {
      gid = uid;
      if(adb_find_group_by_gid(gbuf, gn, gid, 0) == 0)
        gid = adb_next_gid(gbuf, gn, 1000);
      create_private_group = 1;
    }
  } else {
    if(adb_find_group_by_gid(gbuf, gn, gid, 0) < 0) {
      dprintf(2, "useradd: gid does not exist: %d\n", gid);
      exit(0);
    }
  }

  memset(&pw, 0, sizeof(pw));
  snprintf(pw.name, sizeof(pw.name), "%s", name);
  snprintf(pw.passwd, sizeof(pw.passwd), "x");
  pw.uid = uid;
  pw.gid = gid;
  snprintf(pw.gecos, sizeof(pw.gecos), "%s", gecos);
  if(home) {
    snprintf(pw.home, sizeof(pw.home), "%s", home);
  } else {
    snprintf(pw.home, sizeof(pw.home), "/home/%s", name);
  }
  snprintf(pw.shell, sizeof(pw.shell), "%s", shell);

  if(create_private_group) {
    memset(&gr, 0, sizeof(gr));
    snprintf(gr.name, sizeof(gr.name), "%s", name);
    snprintf(gr.passwd, sizeof(gr.passwd), "x");
    gr.gid = gid;
    gr.members[0] = 0;

    goutn = 0;
    if(gn >= (int)sizeof(gout) - 2) {
      dprintf(2, "useradd: /etc/group too large\n");
      exit(0);
    }
    if(gn > 0) {
      memmove(gout, gbuf, gn);
      goutn = gn;
      if(gout[goutn - 1] != '\n')
        gout[goutn++] = '\n';
    }
    if(adb_append_group_line(gout, &goutn, sizeof(gout), &gr) < 0) {
      dprintf(2, "useradd: failed to append private group\n");
      exit(0);
    }

    if(adb_write_file_atomic("/etc/group", "/etc/group.tmp", gout, goutn) < 0) {
      dprintf(2, "useradd: failed to update /etc/group\n");
      exit(0);
    }
  }

  poutn = 0;
  if(pn >= (int)sizeof(pout) - 2) {
    dprintf(2, "useradd: /etc/passwd too large\n");
    exit(0);
  }
  if(pn > 0) {
    memmove(pout, pbuf, pn);
    poutn = pn;
    if(pout[poutn - 1] != '\n')
      pout[poutn++] = '\n';
  }
  if(adb_append_passwd_line(pout, &poutn, sizeof(pout), &pw) < 0) {
    dprintf(2, "useradd: failed to append user\n");
    exit(0);
  }

  if(adb_write_file_atomic("/etc/passwd", "/etc/passwd.tmp", pout, poutn) < 0) {
    dprintf(2, "useradd: failed to update /etc/passwd\n");
    exit(0);
  }

  if(!no_home)
    mkdir(pw.home);

  dprintf(1, "useradd: added %s (uid=%d gid=%d)\n", pw.name, pw.uid, pw.gid);
  exit(0);
}
