/*
 * grep - search for lines matching a pattern.
 *
 * Uses the full regex engine (user/regex.c) supporting BRE by default
 * and ERE with -E.  Flags: -E -i -n -v -c -l -h -H -r.
 */

#include "types.h"
#include "stat.h"
#include "fcntl.h"
#include "errno.h"
#include "dirent.h"
#include "string.h"
#include "regex.h"
#include "auxv6/user.h"

#define LINE_MAX_LOCAL 4096

struct grep_opts {
  int extended;    /* -E: use ERE */
  int icase;       /* -i: case-insensitive */
  int invert;      /* -v: invert match */
  int number;      /* -n: print line numbers */
  int count;       /* -c: print match count per file */
  int files_match; /* -l: print only filenames with matches */
  int no_filename; /* -h: suppress filename prefix */
  int with_filename;/* -H: force filename prefix */
  int recursive;   /* -r: recurse into directories */
};

static struct grep_opts g_opts;
static regex_t g_re;
static int g_any_match;  /* set to 1 if any match was found across all files */

static void
usage(void)
{
  dprintf(2, "usage: grep [-EHhinrvc] [-l] pattern [file...]\n");
  exit(2);
}

static void
grep_fd(int fd, const char *fname, int print_fname)
{
  static char line[LINE_MAX_LOCAL];
  int lineno;
  int pos;
  int n;
  int match_count;
  int ch;

  lineno      = 0;
  pos         = 0;
  match_count = 0;

  for(;;) {
    /* Read one character at a time to find newlines.
     * Simple but avoids complex boundary management. */
    n = read(fd, &ch, 1);
    if(n <= 0)
      break;

    if(ch == '\n' || pos >= LINE_MAX_LOCAL - 1) {
      line[pos] = '\0';
      lineno++;

      /* Match */
      int matched = (regexec(&g_re, line, 0, 0, 0) == REG_OK);
      if(g_opts.invert)
        matched = !matched;

      if(matched) {
        match_count++;
        g_any_match = 1;

        if(!g_opts.count && !g_opts.files_match) {
          if(print_fname)
            dprintf(1, "%s:", fname);
          if(g_opts.number)
            dprintf(1, "%d:", lineno);
          dprintf(1, "%s\n", line);
        }
        if(g_opts.files_match) {
          dprintf(1, "%s\n", fname);
          return;  /* stop after first match for -l */
        }
      }

      pos = 0;
      if(ch != '\n')
        line[pos++] = (char)ch;
    } else {
      line[pos++] = (char)ch;
    }
  }

  /* Handle last line if it didn't end with newline */
  if(pos > 0) {
    line[pos] = '\0';
    lineno++;

    int matched = (regexec(&g_re, line, 0, 0, 0) == REG_OK);
    if(g_opts.invert)
      matched = !matched;

    if(matched) {
      match_count++;
      g_any_match = 1;

      if(!g_opts.count && !g_opts.files_match) {
        if(print_fname)
          dprintf(1, "%s:", fname);
        if(g_opts.number)
          dprintf(1, "%d:", lineno);
        dprintf(1, "%s\n", line);
      }
      if(g_opts.files_match) {
        dprintf(1, "%s\n", fname);
        return;
      }
    }
  }

  if(g_opts.count) {
    if(print_fname)
      dprintf(1, "%s:", fname);
    dprintf(1, "%d\n", match_count);
  }
}

static void grep_path(const char *path, int print_fname);

static void
grep_dir(const char *path, int print_fname)
{
  DIR *dp;
  struct dirent *de;
  char child[512];
  int plen;

  dp = opendir(path);
  if(dp == 0) {
    dprintf(2, "grep: cannot open directory '%s': %s\n", path, strerror(errno));
    return;
  }

  plen = strlen(path);
  while((de = readdir(dp)) != 0) {
    int nlen;

    if(strcmp(de->d_name, ".") == 0 || strcmp(de->d_name, "..") == 0)
      continue;

    nlen = strlen(de->d_name);
    if(plen + 1 + nlen + 1 > (int)sizeof(child)) {
      dprintf(2, "grep: path too long, skipping\n");
      continue;
    }
    memmove(child, path, plen);
    if(plen > 0 && child[plen - 1] != '/')
      child[plen++] = '/';
    memmove(child + plen, de->d_name, nlen + 1);

    grep_path(child, 1);
  }
  closedir(dp);
}

static void
grep_path(const char *path, int print_fname)
{
  int fd;
  struct stat st;

  if(stat(path, &st) < 0) {
    dprintf(2, "grep: cannot stat '%s': %s\n", path, strerror(errno));
    return;
  }

  if(st.st_type == T_DIR) {
    if(g_opts.recursive) {
      grep_dir(path, print_fname);
    } else {
      dprintf(2, "grep: '%s': Is a directory\n", path);
    }
    return;
  }

  fd = open(path, O_RDONLY);
  if(fd < 0) {
    dprintf(2, "grep: cannot open '%s': %s\n", path, strerror(errno));
    return;
  }

  grep_fd(fd, path, print_fname);
  close(fd);
}

int
main(int argc, char *argv[])
{
  int i;
  int cflags;
  char errbuf[128];
  const char *pattern;
  int nfiles;
  int print_fname;

  g_opts.extended     = 0;
  g_opts.icase        = 0;
  g_opts.invert       = 0;
  g_opts.number       = 0;
  g_opts.count        = 0;
  g_opts.files_match  = 0;
  g_opts.no_filename  = 0;
  g_opts.with_filename= 0;
  g_opts.recursive    = 0;
  g_any_match         = 0;

  for(i = 1; i < argc && argv[i][0] == '-' && argv[i][1] != '\0'; i++) {
    char *f;

    for(f = argv[i] + 1; *f; f++) {
      switch(*f) {
      case 'E': g_opts.extended      = 1; break;
      case 'i': g_opts.icase         = 1; break;
      case 'v': g_opts.invert        = 1; break;
      case 'n': g_opts.number        = 1; break;
      case 'c': g_opts.count         = 1; break;
      case 'l': g_opts.files_match   = 1; break;
      case 'h': g_opts.no_filename   = 1; break;
      case 'H': g_opts.with_filename = 1; break;
      case 'r': g_opts.recursive     = 1; break;
      case '-': goto done_flags;          /* -- ends flags */
      default:
        dprintf(2, "grep: unknown option '-%c'\n", *f);
        usage();
      }
    }
  }
done_flags:

  if(i >= argc)
    usage();

  pattern = argv[i++];
  nfiles  = argc - i;

  cflags = 0;
  if(g_opts.extended) cflags |= REG_EXTENDED;
  if(g_opts.icase)    cflags |= REG_ICASE;
  cflags |= REG_NOSUB;

  if(regcomp(&g_re, pattern, cflags) != REG_OK) {
    regerror(REG_BADPAT, &g_re, errbuf, sizeof(errbuf));
    dprintf(2, "grep: invalid pattern: %s\n", errbuf);
    exit(2);
  }

  /* Decide whether to prefix matches with the filename */
  if(g_opts.no_filename)
    print_fname = 0;
  else if(g_opts.with_filename)
    print_fname = 1;
  else
    print_fname = (nfiles > 1 || g_opts.recursive);

  if(nfiles == 0) {
    grep_fd(0, "(standard input)", g_opts.with_filename);
  } else {
    for(; i < argc; i++)
      grep_path(argv[i], print_fname);
  }

  regfree(&g_re);
  exit(g_any_match ? 0 : 1);
}


