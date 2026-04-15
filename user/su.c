#include "types.h"
#include "crypt.h"
#include "pwd.h"
#include "shadow.h"
#include "sys/stat.h"
#include "auxv6/user.h"
#include "fcntl.h"

#define USER_MAX 32

static void
trim_trailing_ws(char *s)
{
  int n;

  n = strlen(s);
  while(n > 0 && (s[n - 1] == ' ' || s[n - 1] == '\t' ||
                  s[n - 1] == '\r' || s[n - 1] == '\n'))
    n--;
  s[n] = 0;
}

static int
is_locked_password(const char *stored)
{
  if(stored == 0)
    return 1;
  if(stored[0] == 0)
    return 0;
  return (strcmp(stored, "*") == 0 || strcmp(stored, "!") == 0 ||
          strcmp(stored, "x") == 0);
}

static int
verify_password(const char *input, const char *stored)
{
  char *calc;

  if(input == 0 || stored == 0)
    return 0;
  if(is_locked_password(stored))
    return 0;
  if(strncmp(stored, "$aux$", 5) == 0) {
    calc = crypt(input, stored);
    return (calc != 0 && strcmp(calc, stored) == 0);
  }
  return strcmp(input, stored) == 0;
}

int
main(int argc, char *argv[])
{
  int uid;
  char pass[USER_MAX];
  char *target;
  char *sh_argv[2];
  struct passwd *ent;
  struct spwd *sp;
  const char *auth_pass;

  if(argc > 2) {
    dprintf(2, "usage: su [user]\n");
    exit(0);
  }

  target = (argc == 2) ? argv[1] : "root";
  ent = getpwnam(target);
  if(ent == 0) {
    dprintf(2, "su: unknown user %s\n", target);
    exit(0);
  }

  uid = getuid();
  if(uid < 0)
    uid = 0;

  if(uid != 0) {
    dprintf(1, "Password: ");
    memset(pass, 0, sizeof(pass));
    if(readpass(pass, sizeof(pass)) == 0)
      exit(0);
    trim_trailing_ws(pass);

    auth_pass = ent->pw_passwd;
    sp = getspnam(target);
    if(sp && sp->sp_pwdp && sp->sp_pwdp[0] &&
       strcmp(sp->sp_pwdp, "x") != 0 && strcmp(sp->sp_pwdp, "*") != 0 &&
       strcmp(sp->sp_pwdp, "!") != 0)
      auth_pass = sp->sp_pwdp;

    if(!verify_password(pass, auth_pass)) {
      dprintf(2, "su: authentication failed\n");
      exit(0);
    }
  }

  {
    gid_t groups[1];

    groups[0] = ent->pw_gid;
    if(setgroups(1, groups) < 0 || setgid(ent->pw_gid) < 0 || setuid(ent->pw_uid) < 0) {
      dprintf(2, "su: permission denied\n");
      exit(0);
    }
  }

  if(ent->pw_dir[0])
    chdir(ent->pw_dir);

  sh_argv[0] = (ent->pw_shell && ent->pw_shell[0]) ? ent->pw_shell : "/bin/sh";
  sh_argv[1] = 0;
  exec(sh_argv[0], sh_argv);

  dprintf(2, "su: exec %s failed\n", sh_argv[0]);
  exit(0);
}
