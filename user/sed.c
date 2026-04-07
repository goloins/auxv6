#include "types.h"
#include "stdio.h"
#include "stdlib.h"
#include "string.h"
#include "regex.h"
#include "auxv6/user.h"

#define SED_MAX_CMDS 16
#define SED_LINE_MAX 4096

enum sed_cmd_kind {
  SED_CMD_SUB,
  SED_CMD_PRINT,
  SED_CMD_DELETE,
};

struct sed_cmd {
  enum sed_cmd_kind kind;
  int has_addr;
  regex_t addr_re;
  regex_t sub_re;
  int sub_global;
  char repl[256];
};

static struct sed_cmd g_cmds[SED_MAX_CMDS];
static int g_ncmd;
static int g_suppress_default;

static void
usage(void)
{
  dprintf(2, "usage: sed [-n] script [file ...]\n");
  dprintf(2, "       sed [-n] -e script [-e script ...] [file ...]\n");
  exit(1);
}

static int
copy_until_delim(char *dst, int dsz, const char **pp, char delim)
{
  const char *p;
  int j;

  p = *pp;
  j = 0;
  while(*p && *p != delim) {
    if(*p == '\\' && p[1] != '\0')
      p++;
    if(j + 1 >= dsz)
      return -1;
    dst[j++] = *p++;
  }
  if(*p != delim)
    return -1;
  dst[j] = '\0';
  *pp = p + 1;
  return 0;
}

static int
parse_one_cmd(const char **pp)
{
  struct sed_cmd *c;
  const char *p;
  char tmp[256];

  if(g_ncmd >= SED_MAX_CMDS)
    return -1;

  p = *pp;
  c = &g_cmds[g_ncmd];
  memset(c, 0, sizeof(*c));

  while(*p == ' ' || *p == '\t' || *p == ';')
    p++;
  if(*p == '\0') {
    *pp = p;
    return 0;
  }

  if(*p == '/') {
    p++;
    if(copy_until_delim(tmp, sizeof(tmp), &p, '/') < 0)
      return -1;
    if(regcomp(&c->addr_re, tmp, REG_EXTENDED) != 0)
      return -1;
    c->has_addr = 1;
    while(*p == ' ' || *p == '\t')
      p++;
  }

  if(*p == 'p') {
    c->kind = SED_CMD_PRINT;
    p++;
  } else if(*p == 'd') {
    c->kind = SED_CMD_DELETE;
    p++;
  } else if(*p == 's') {
    char delim;
    int rc;

    c->kind = SED_CMD_SUB;
    p++;
    delim = *p++;
    if(delim == '\0')
      return -1;

    rc = copy_until_delim(tmp, sizeof(tmp), &p, delim);
    if(rc < 0 || regcomp(&c->sub_re, tmp, REG_EXTENDED) != 0)
      return -1;

    if(copy_until_delim(c->repl, sizeof(c->repl), &p, delim) < 0)
      return -1;

    while(*p == 'g') {
      c->sub_global = 1;
      p++;
    }
  } else {
    return -1;
  }

  while(*p == ' ' || *p == '\t')
    p++;
  if(*p == ';')
    p++;

  *pp = p;
  g_ncmd++;
  return 1;
}

static int
parse_script(const char *script)
{
  const char *p;
  int rc;

  p = script;
  while(*p) {
    rc = parse_one_cmd(&p);
    if(rc < 0)
      return -1;
    if(rc == 0)
      break;
  }
  return 0;
}

static int
address_matches(struct sed_cmd *c, const char *line)
{
  if(!c->has_addr)
    return 1;
  return regexec(&c->addr_re, line, 0, 0, 0) == 0;
}

static int
append_text(char *dst, int dsz, int *dlen, const char *src, int n)
{
  if(*dlen + n >= dsz)
    return -1;
  memmove(dst + *dlen, src, n);
  *dlen += n;
  dst[*dlen] = '\0';
  return 0;
}

static int
append_repl(char *dst, int dsz, int *dlen, const char *repl,
            const char *match, int mlen)
{
  int i;

  for(i = 0; repl[i]; i++) {
    if(repl[i] == '&') {
      if(append_text(dst, dsz, dlen, match, mlen) < 0)
        return -1;
      continue;
    }
    if(repl[i] == '\\' && repl[i + 1])
      i++;
    if(append_text(dst, dsz, dlen, &repl[i], 1) < 0)
      return -1;
  }
  return 0;
}

static int
apply_sub(struct sed_cmd *c, char *line, int line_sz)
{
  char out[SED_LINE_MAX];
  regmatch_t m[1];
  int src_off;
  int out_len;
  int changed;

  src_off = 0;
  out_len = 0;
  changed = 0;
  out[0] = '\0';

  while(regexec(&c->sub_re, line + src_off, 1, m, 0) == 0) {
    int so;
    int eo;

    so = m[0].rm_so;
    eo = m[0].rm_eo;
    if(so < 0 || eo < so)
      return -1;

    if(append_text(out, sizeof(out), &out_len, line + src_off, so) < 0)
      return -1;

    if(append_repl(out, sizeof(out), &out_len, c->repl,
                    line + src_off + so, eo - so) < 0)
      return -1;

    changed = 1;
    src_off += eo;

    if(!c->sub_global)
      break;

    if(eo == so) {
      if(line[src_off] == '\0')
        break;
      if(append_text(out, sizeof(out), &out_len, line + src_off, 1) < 0)
        return -1;
      src_off++;
    }
  }

  if(!changed)
    return 0;

  if(append_text(out, sizeof(out), &out_len, line + src_off,
                 strlen(line + src_off)) < 0)
    return -1;

  if((int)strlen(out) >= line_sz)
    return -1;
  strcpy(line, out);
  return 1;
}

static int
run_stream(FILE *fp)
{
  char line[SED_LINE_MAX];

  while(fgets(line, sizeof(line), fp) != 0) {
    int deleted;
    int cidx;

    deleted = 0;
    for(cidx = 0; cidx < g_ncmd; cidx++) {
      struct sed_cmd *c;

      c = &g_cmds[cidx];
      if(!address_matches(c, line))
        continue;

      if(c->kind == SED_CMD_DELETE) {
        deleted = 1;
        break;
      }
      if(c->kind == SED_CMD_PRINT) {
        fputs(line, stdout);
        continue;
      }
      if(c->kind == SED_CMD_SUB) {
        if(apply_sub(c, line, sizeof(line)) < 0) {
          dprintf(2, "sed: substitution overflow\n");
          return -1;
        }
      }
    }

    if(!deleted && !g_suppress_default)
      fputs(line, stdout);
  }

  return 0;
}

int
main(int argc, char *argv[])
{
  const char *script;
  int argi;
  int i;

  script = 0;
  argi = 1;

  while(argi < argc && argv[argi][0] == '-') {
    if(strcmp(argv[argi], "-n") == 0) {
      g_suppress_default = 1;
      argi++;
      continue;
    }
    if(strcmp(argv[argi], "-e") == 0) {
      if(argi + 1 >= argc)
        usage();
      if(parse_script(argv[argi + 1]) < 0) {
        dprintf(2, "sed: invalid script: %s\n", argv[argi + 1]);
        exit(1);
      }
      argi += 2;
      continue;
    }
    break;
  }

  if(g_ncmd == 0) {
    if(argi >= argc)
      usage();
    script = argv[argi++];
    if(parse_script(script) < 0) {
      dprintf(2, "sed: invalid script: %s\n", script);
      exit(1);
    }
  }

  if(argi >= argc) {
    if(run_stream(stdin) < 0)
      exit(1);
    exit(0);
  }

  for(i = argi; i < argc; i++) {
    FILE *fp;

    fp = fopen(argv[i], "r");
    if(fp == 0) {
      dprintf(2, "sed: cannot open %s\n", argv[i]);
      exit(1);
    }
    if(run_stream(fp) < 0) {
      fclose(fp);
      exit(1);
    }
    fclose(fp);
  }

  exit(0);
}
