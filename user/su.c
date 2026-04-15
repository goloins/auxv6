#include "types.h"
#include "crypt.h"
#include "pwd.h"
#include "grp.h"
#include "shadow.h"
#include "sys/stat.h"
#include "auxv6/user.h"
#include "fcntl.h"
#include "stdlib.h"
#include "stdio.h"
#include "unistd.h"

#define USER_MAX 64
#define SU_MAX_ARGS 128
#define SU_MAX_SUPP_GROUPS 32
#define SU_MAX_WHITELIST 32
#define SU_ENV_NAME_MAX 64
#define SU_ENV_VALUE_MAX 256

struct su_opts {
  int login_shell;
  int preserve_env;
  int fast_shell;
  int use_pty;
  char *command;
  char *session_command;
  char *shell;
  char *primary_group;
  char *supp_groups[SU_MAX_SUPP_GROUPS];
  int supp_group_count;
  char *whitelist;
  char *target;
  int shell_arg_index;
};

struct su_env_keep {
  char name[SU_ENV_NAME_MAX];
  char value[SU_ENV_VALUE_MAX];
};

static void
usage(void)
{
  dprintf(2,
          "usage: su [options] [-] [user [args...]]\n"
          "  - , -l, --login                login shell\n"
          "  -c, --command CMD              pass command to shell with -c\n"
          "      --session-command CMD      like --command (compat)\n"
          "  -s, --shell SHELL              use alternate shell\n"
          "  -m, -p, --preserve-environment keep current environment\n"
          "  -w, --whitelist-environment L  preserve env names in login mode\n"
          "  -g, --group GROUP              set primary group\n"
          "  -G, --supp-group GROUP         add supplementary group (repeatable)\n"
          "  -f, --fast                     pass -f to shell\n"
          "  -P, --pty                      accepted (pty allocation not implemented)\n"
          "  -h, --help                     show help\n"
          "  -V, --version                  show version\n");
}

static void
version(void)
{
  dprintf(1, "su (auxv6) 1.1\n");
}

static int
is_numeric(const char *s)
{
  int i;

  if(s == 0 || s[0] == 0)
    return 0;

  for(i = 0; s[i]; i++) {
    if(s[i] < '0' || s[i] > '9')
      return 0;
  }
  return 1;
}

static const char *
base_name(const char *path)
{
  const char *base;
  int i;

  if(path == 0 || path[0] == 0)
    return "sh";

  base = path;
  for(i = 0; path[i]; i++)
    if(path[i] == '/')
      base = path + i + 1;
  return base;
}

static int
copy_field(char *dst, int dsz, const char *src)
{
  int n;

  if(dst == 0 || dsz <= 0)
    return -1;
  if(src == 0) {
    dst[0] = 0;
    return 0;
  }

  n = strlen(src);
  if(n >= dsz)
    return -1;
  memmove(dst, src, n);
  dst[n] = 0;
  return 0;
}

static int
parse_group(const char *name_or_gid, gid_t *out)
{
  struct group *gr;

  if(name_or_gid == 0 || out == 0)
    return -1;

  if(is_numeric(name_or_gid)) {
    *out = (gid_t)atoi(name_or_gid);
    return 0;
  }

  gr = getgrnam(name_or_gid);
  if(gr == 0)
    return -1;
  *out = gr->gr_gid;
  return 0;
}

static void
add_supp_group(struct su_opts *opts, char *name)
{
  if(opts->supp_group_count >= SU_MAX_SUPP_GROUPS) {
    dprintf(2, "su: too many supplementary groups (max %d)\n", SU_MAX_SUPP_GROUPS);
    exit(1);
  }
  opts->supp_groups[opts->supp_group_count++] = name;
}

static int
parse_opt_value(int argc, char *argv[], int *idx, const char *arg, const char *name, char **out)
{
  int namelen;

  namelen = strlen(name);
  if(strncmp(arg, name, namelen) == 0) {
    if(arg[namelen] == '=') {
      *out = (char *)(arg + namelen + 1);
      return 1;
    }
    if(arg[namelen] == 0) {
      if(*idx + 1 >= argc) {
        dprintf(2, "su: option '%s' requires an argument\n", name);
        return -1;
      }
      *out = argv[++(*idx)];
      return 1;
    }
  }
  return 0;
}

static int
parse_options(int argc, char *argv[], struct su_opts *opts)
{
  int i;

  memset(opts, 0, sizeof(*opts));
  opts->target = "root";

  for(i = 1; i < argc; i++) {
    char *a;

    a = argv[i];
    if(strcmp(a, "--") == 0) {
      i++;
      break;
    }
    if(strcmp(a, "-") == 0) {
      opts->login_shell = 1;
      continue;
    }
    if(a[0] != '-' || a[1] == 0)
      break;

    if(strncmp(a, "--", 2) == 0) {
      char *val;
      int r;

      if(strcmp(a, "--login") == 0)
        opts->login_shell = 1;
      else if(strcmp(a, "--preserve-environment") == 0)
        opts->preserve_env = 1;
      else if(strcmp(a, "--fast") == 0)
        opts->fast_shell = 1;
      else if(strcmp(a, "--pty") == 0)
        opts->use_pty = 1;
      else if(strcmp(a, "--help") == 0) {
        usage();
        exit(0);
      } else if(strcmp(a, "--version") == 0) {
        version();
        exit(0);
      } else {
        val = 0;

        r = parse_opt_value(argc, argv, &i, a, "--command", &val);
        if(r < 0)
          return -1;
        if(r > 0)
          opts->command = val;

        if(r == 0) {
          r = parse_opt_value(argc, argv, &i, a, "--session-command", &val);
          if(r < 0)
            return -1;
          if(r > 0)
            opts->session_command = val;
        }

        if(r == 0) {
          r = parse_opt_value(argc, argv, &i, a, "--shell", &val);
          if(r < 0)
            return -1;
          if(r > 0)
            opts->shell = val;
        }

        if(r == 0) {
          r = parse_opt_value(argc, argv, &i, a, "--group", &val);
          if(r < 0)
            return -1;
          if(r > 0)
            opts->primary_group = val;
        }

        if(r == 0) {
          r = parse_opt_value(argc, argv, &i, a, "--supp-group", &val);
          if(r < 0)
            return -1;
          if(r > 0)
            add_supp_group(opts, val);
        }

        if(r == 0) {
          r = parse_opt_value(argc, argv, &i, a, "--whitelist-environment", &val);
          if(r < 0)
            return -1;
          if(r > 0)
            opts->whitelist = val;
        }

        if(r == 0) {
          dprintf(2, "su: unrecognized option '%s'\n", a);
          usage();
          return -1;
        }
      }
      continue;
    }

    {
      int k;

      for(k = 1; a[k]; k++) {
        char c;

        c = a[k];
        if(c == 'l')
          opts->login_shell = 1;
        else if(c == 'm' || c == 'p')
          opts->preserve_env = 1;
        else if(c == 'f')
          opts->fast_shell = 1;
        else if(c == 'P')
          opts->use_pty = 1;
        else if(c == 'h') {
          usage();
          exit(0);
        } else if(c == 'V') {
          version();
          exit(0);
        } else if(c == 'c' || c == 's' || c == 'g' || c == 'G' || c == 'w') {
          char *val;

          if(a[k + 1])
            val = &a[k + 1];
          else {
            if(i + 1 >= argc) {
              dprintf(2, "su: option '-%c' requires an argument\n", c);
              return -1;
            }
            val = argv[++i];
          }

          if(c == 'c')
            opts->command = val;
          else if(c == 's')
            opts->shell = val;
          else if(c == 'g')
            opts->primary_group = val;
          else if(c == 'G')
            add_supp_group(opts, val);
          else if(c == 'w')
            opts->whitelist = val;
          break;
        } else {
          dprintf(2, "su: invalid option -- '%c'\n", c);
          usage();
          return -1;
        }
      }
    }
  }

  if(i < argc) {
    opts->target = argv[i];
    i++;
  }
  opts->shell_arg_index = i;

  if(opts->command && opts->session_command) {
    dprintf(2, "su: --command and --session-command are mutually exclusive\n");
    return -1;
  }
  if(opts->login_shell)
    opts->preserve_env = 0;

  return 0;
}

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

static int
is_env_name_char(char c)
{
  if((c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z') ||
     (c >= '0' && c <= '9') || c == '_')
    return 1;
  return 0;
}

static int
is_valid_env_name(const char *name)
{
  int i;

  if(name == 0 || name[0] == 0)
    return 0;
  if((name[0] >= '0' && name[0] <= '9') || name[0] == '=')
    return 0;

  for(i = 0; name[i]; i++) {
    if(name[i] == '=')
      return 0;
    if(!is_env_name_char(name[i]))
      return 0;
  }
  return 1;
}

static void
collect_whitelist(const char *list, struct su_env_keep *kept, int *kept_n)
{
  int i;

  if(kept_n == 0)
    return;
  *kept_n = 0;
  if(list == 0 || list[0] == 0)
    return;

  i = 0;
  while(list[i]) {
    int start;
    int end;
    int len;
    char name[SU_ENV_NAME_MAX];
    char *val;

    while(list[i] == ' ' || list[i] == '\t' || list[i] == ',')
      i++;
    if(list[i] == 0)
      break;

    start = i;
    while(list[i] && list[i] != ',')
      i++;
    end = i;
    while(end > start && (list[end - 1] == ' ' || list[end - 1] == '\t'))
      end--;

    len = end - start;
    if(len <= 0)
      continue;
    if(len >= (int)sizeof(name))
      continue;
    memmove(name, list + start, len);
    name[len] = 0;
    if(!is_valid_env_name(name))
      continue;

    if(*kept_n >= SU_MAX_WHITELIST)
      continue;

    val = getenv(name);
    if(val == 0)
      continue;
    if(copy_field(kept[*kept_n].name, sizeof(kept[*kept_n].name), name) < 0)
      continue;
    if(copy_field(kept[*kept_n].value, sizeof(kept[*kept_n].value), val) < 0)
      continue;
    (*kept_n)++;
  }
}

static void
set_su_env(const struct su_opts *opts, struct passwd *ent)
{
  struct su_env_keep kept[SU_MAX_WHITELIST];
  int kept_n;
  char *term;

  if(opts == 0 || ent == 0)
    return;

  term = getenv("TERM");
  kept_n = 0;

  if(opts->login_shell) {
    collect_whitelist(opts->whitelist, kept, &kept_n);
    clearenv();
    setenv("HOME", ent->pw_dir && ent->pw_dir[0] ? ent->pw_dir : "/", 1);
    setenv("SHELL", (opts->shell && opts->shell[0]) ? opts->shell :
                   ((ent->pw_shell && ent->pw_shell[0]) ? ent->pw_shell : "/bin/sh"), 1);
    setenv("USER", ent->pw_name, 1);
    setenv("LOGNAME", ent->pw_name, 1);
    if(ent->pw_uid == 0)
      setenv("PATH", "/sbin:/bin:/usr/sbin:/usr/bin", 1);
    else
      setenv("PATH", "/bin:/usr/bin:/sbin:/usr/sbin", 1);

    if(term && term[0])
      setenv("TERM", term, 1);

    {
      int i;

      for(i = 0; i < kept_n; i++)
        setenv(kept[i].name, kept[i].value, 1);
    }
  } else if(!opts->preserve_env) {
    setenv("HOME", ent->pw_dir && ent->pw_dir[0] ? ent->pw_dir : "/", 1);
    setenv("SHELL", (opts->shell && opts->shell[0]) ? opts->shell :
                   ((ent->pw_shell && ent->pw_shell[0]) ? ent->pw_shell : "/bin/sh"), 1);
    setenv("USER", ent->pw_name, 1);
    setenv("LOGNAME", ent->pw_name, 1);
  }
}

static int
build_groups(const struct su_opts *opts, struct passwd *ent, gid_t *out, int max)
{
  gid_t primary;
  int n;
  int i;

  if(opts == 0 || ent == 0 || out == 0 || max <= 0)
    return -1;

  primary = ent->pw_gid;
  if(opts->primary_group) {
    if(parse_group(opts->primary_group, &primary) < 0)
      return -1;
  }

  n = 0;
  out[n++] = primary;
  for(i = 0; i < opts->supp_group_count; i++) {
    gid_t g;
    int j;
    int dup;

    if(parse_group(opts->supp_groups[i], &g) < 0)
      return -1;

    dup = 0;
    for(j = 0; j < n; j++) {
      if(out[j] == g) {
        dup = 1;
        break;
      }
    }
    if(dup)
      continue;
    if(n >= max)
      return -1;
    out[n++] = g;
  }

  return n;
}

int
main(int argc, char *argv[])
{
  int uid;
  char pass[USER_MAX];
  char login_argv0[64];
  char **sh_argv;
  struct passwd *ent;
  struct spwd *sp;
  const char *auth_pass;
  struct su_opts opts;
  const char *shell;
  const char *shell_base;
  const char *cmd;
  int sh_argc;
  int i;

  if(parse_options(argc, argv, &opts) < 0)
    exit(1);

  ent = getpwnam(opts.target);
  if(ent == 0) {
    dprintf(2, "su: unknown user %s\n", opts.target);
    exit(1);
  }

  if(opts.use_pty)
    dprintf(2, "su: note: --pty accepted, pseudo-tty allocation not implemented\n");

  if(opts.primary_group || opts.supp_group_count > 0) {
    if(getuid() != 0) {
      dprintf(2, "su: only root may specify alternate groups\n");
      exit(1);
    }
  }

  uid = getuid();
  if(uid < 0)
    uid = 0;

  if(uid != 0) {
    dprintf(1, "Password: ");
    memset(pass, 0, sizeof(pass));
    if(readpass(pass, sizeof(pass)) == 0)
      exit(1);
    trim_trailing_ws(pass);

    auth_pass = ent->pw_passwd;
    sp = getspnam(opts.target);
    if(sp && sp->sp_pwdp && sp->sp_pwdp[0] &&
       strcmp(sp->sp_pwdp, "x") != 0 && strcmp(sp->sp_pwdp, "*") != 0 &&
       strcmp(sp->sp_pwdp, "!") != 0)
      auth_pass = sp->sp_pwdp;

    if(!verify_password(pass, auth_pass)) {
      dprintf(2, "su: authentication failed\n");
      exit(1);
    }
  }

  {
    gid_t groups[1 + SU_MAX_SUPP_GROUPS];
    gid_t primary_gid;
    int n;

    if(opts.primary_group || opts.supp_group_count > 0) {
      n = build_groups(&opts, ent, groups, sizeof(groups) / sizeof(groups[0]));
      if(n < 1) {
        dprintf(2, "su: invalid group selection\n");
        exit(1);
      }
      primary_gid = groups[0];
      if(setgroups(n, groups) < 0 || setgid(primary_gid) < 0 || setuid(ent->pw_uid) < 0) {
        dprintf(2, "su: permission denied\n");
        exit(1);
      }
    } else {
      primary_gid = ent->pw_gid;
      if(initgroups(opts.target, primary_gid) < 0) {
        groups[0] = primary_gid;
        if(setgroups(1, groups) < 0) {
          dprintf(2, "su: failed to set groups\n");
          exit(1);
        }
      }
      if(setgid(primary_gid) < 0 || setuid(ent->pw_uid) < 0) {
        dprintf(2, "su: permission denied\n");
        exit(1);
      }
    }
  }

  set_su_env(&opts, ent);

  if(opts.login_shell && ent->pw_dir[0])
    chdir(ent->pw_dir);

  shell = (opts.shell && opts.shell[0]) ? opts.shell :
          ((ent->pw_shell && ent->pw_shell[0]) ? ent->pw_shell : "/bin/sh");
  shell_base = base_name(shell);

  cmd = opts.command ? opts.command : opts.session_command;
  sh_argv = malloc(sizeof(char *) * SU_MAX_ARGS);
  if(sh_argv == 0) {
    dprintf(2, "su: out of memory\n");
    exit(1);
  }

  sh_argc = 0;
  if(opts.login_shell) {
    snprintf(login_argv0, sizeof(login_argv0), "-%s", shell_base);
    sh_argv[sh_argc++] = login_argv0;
  } else {
    sh_argv[sh_argc++] = (char *)shell_base;
  }

  if(opts.fast_shell)
    sh_argv[sh_argc++] = "-f";

  if(cmd) {
    sh_argv[sh_argc++] = "-c";
    sh_argv[sh_argc++] = (char *)cmd;
  } else {
    for(i = opts.shell_arg_index; i < argc && sh_argc + 1 < SU_MAX_ARGS; i++)
      sh_argv[sh_argc++] = argv[i];
  }
  sh_argv[sh_argc] = 0;

  exec((char *)shell, sh_argv);

  dprintf(2, "su: exec %s failed\n", shell);
  exit(1);
}
