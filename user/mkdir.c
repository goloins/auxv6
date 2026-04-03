#include "types.h"
#include "stat.h"
#include "errno.h"
#include "string.h"
#include "stdlib.h"
#include "limits.h"
#include "auxv6/user.h"

struct mkdir_opts {
  int parents;
  int verbose;
  int have_mode;
  int mode;
};

static void
usage(void)
{
  dprintf(2, "usage: mkdir [-pv] [-m mode] directory...\n");
  exit(1);
}

/*
 * mkdir_p - create directory and any missing parent components.
 * Returns 0 on success, -1 on error (errno set by the failing syscall).
 */
static int
mkdir_p(const char *path, int verbose)
{
  char buf[PATH_MAX];
  char *p;
  struct stat st;
  size_t len;

  len = strlen(path);
  if(len == 0 || len >= sizeof(buf)) {
    errno = ENAMETOOLONG;
    return -1;
  }
  memmove(buf, path, len + 1);

  /* Walk the path, creating each component */
  for(p = buf + 1; *p; p++) {
    if(*p != '/')
      continue;

    *p = '\0';
    if(stat(buf, &st) < 0) {
      if(mkdir(buf) < 0 && errno != EEXIST) {
        *p = '/';
        return -1;
      }
      if(verbose)
        dprintf(1, "mkdir: created directory '%s'\n", buf);
    }
    *p = '/';
  }

  /* Create the final component */
  if(stat(path, &st) == 0)
    return 0;   /* already exists: -p silently succeeds */

  if(mkdir(path) < 0)
    return -1;

  if(verbose)
    dprintf(1, "mkdir: created directory '%s'\n", path);

  return 0;
}

int
main(int argc, char *argv[])
{
  struct mkdir_opts opts;
  int i;
  int status;

  opts.parents   = 0;
  opts.verbose   = 0;
  opts.have_mode = 0;
  opts.mode      = 0755;
  status         = 0;

  /* Parse flags */
  for(i = 1; i < argc && argv[i][0] == '-' && argv[i][1] != '\0'; i++) {
    char *f;

    for(f = argv[i] + 1; *f; f++) {
      switch(*f) {
      case 'p': opts.parents = 1; break;
      case 'v': opts.verbose = 1; break;
      case 'm':
        /* -m mode: mode may be attached (-m755) or the next arg */
        if(f[1] != '\0') {
          opts.mode = (int)strtol(f + 1, 0, 8);
          opts.have_mode = 1;
          f += strlen(f + 1); /* consume rest of this flag word */
        } else if(i + 1 < argc) {
          i++;
          opts.mode = (int)strtol(argv[i], 0, 8);
          opts.have_mode = 1;
        } else {
          dprintf(2, "mkdir: option requires an argument -- 'm'\n");
          usage();
        }
        break;
      default:
        dprintf(2, "mkdir: unknown option '-%c'\n", *f);
        usage();
      }
    }
  }

  if(i >= argc)
    usage();

  for(; i < argc; i++) {
    int err;

    if(opts.parents) {
      err = mkdir_p(argv[i], opts.verbose);
    } else {
      err = mkdir(argv[i]);
      if(err < 0) {
        dprintf(2, "mkdir: cannot create directory '%s': %s\n",
                argv[i], strerror(errno));
        status = 1;
        continue;
      }
      if(opts.verbose)
        dprintf(1, "mkdir: created directory '%s'\n", argv[i]);
    }

    if(err < 0) {
      dprintf(2, "mkdir: cannot create directory '%s': %s\n",
              argv[i], strerror(errno));
      status = 1;
      continue;
    }

    if(opts.have_mode) {
      if(chmod(argv[i], opts.mode) < 0) {
        dprintf(2, "mkdir: cannot set mode on '%s': %s\n",
                argv[i], strerror(errno));
        status = 1;
      }
    }
  }

  exit(status);
}

