#include "types.h"
#include "stdio.h"
#include "stdlib.h"
#include "string.h"
#include "regex.h"
#include "auxv6/user.h"

#define AWK_MAX_FIELDS 128
#define AWK_MAX_TOKENS 32
#define AWK_LINE_MAX 4096

enum awk_tok_kind {
  AWK_TOK_DOLLAR0,
  AWK_TOK_FIELD,
  AWK_TOK_NR,
  AWK_TOK_STRING,
};

struct awk_token {
  enum awk_tok_kind kind;
  int field_no;
  char text[128];
};

struct awk_prog {
  int has_pattern;
  regex_t pattern;
  int ntok;
  struct awk_token toks[AWK_MAX_TOKENS];
  const char *fs;
};

static void
usage(void)
{
  dprintf(2, "usage: awk [-F fs] '[/regex/] { print [expr[,expr...]] }' [file ...]\n");
  exit(1);
}

static const char *
skip_ws(const char *p)
{
  while(*p == ' ' || *p == '\t' || *p == '\n')
    p++;
  return p;
}

static int
parse_quoted(const char **pp, char *out, int osz)
{
  const char *p;
  int j;

  p = *pp;
  if(*p != '"')
    return -1;
  p++;

  j = 0;
  while(*p && *p != '"') {
    if(*p == '\\' && p[1])
      p++;
    if(j + 1 >= osz)
      return -1;
    out[j++] = *p++;
  }
  if(*p != '"')
    return -1;
  out[j] = '\0';
  *pp = p + 1;
  return 0;
}

static int
parse_program(struct awk_prog *pg, const char *src)
{
  const char *p;
  char pat[128];

  memset(pg, 0, sizeof(*pg));
  pg->fs = " ";

  p = skip_ws(src);
  if(*p == '/') {
    int j;

    p++;
    j = 0;
    while(*p && *p != '/') {
      if(*p == '\\' && p[1])
        p++;
      if(j + 1 >= (int)sizeof(pat))
        return -1;
      pat[j++] = *p++;
    }
    if(*p != '/')
      return -1;
    pat[j] = '\0';
    if(regcomp(&pg->pattern, pat, REG_EXTENDED) != 0)
      return -1;
    pg->has_pattern = 1;
    p++;
    p = skip_ws(p);
  }

  if(*p != '{')
    return -1;
  p++;
  p = skip_ws(p);
  if(strncmp(p, "print", 5) != 0)
    return -1;
  p += 5;
  p = skip_ws(p);

  if(*p == '}') {
    pg->toks[0].kind = AWK_TOK_DOLLAR0;
    pg->ntok = 1;
    return 0;
  }

  while(*p && *p != '}') {
    struct awk_token *tk;

    if(pg->ntok >= AWK_MAX_TOKENS)
      return -1;
    tk = &pg->toks[pg->ntok];
    memset(tk, 0, sizeof(*tk));

    if(*p == '$') {
      p++;
      if(*p == '0') {
        tk->kind = AWK_TOK_DOLLAR0;
        p++;
      } else {
        char *end;
        long n;

        n = strtol(p, &end, 10);
        if(end == p || n <= 0 || n > AWK_MAX_FIELDS)
          return -1;
        tk->kind = AWK_TOK_FIELD;
        tk->field_no = (int)n;
        p = end;
      }
    } else if(*p == 'N' && p[1] == 'R') {
      tk->kind = AWK_TOK_NR;
      p += 2;
    } else if(*p == '"') {
      tk->kind = AWK_TOK_STRING;
      if(parse_quoted(&p, tk->text, sizeof(tk->text)) < 0)
        return -1;
    } else {
      return -1;
    }

    pg->ntok++;
    p = skip_ws(p);
    if(*p == ',') {
      p++;
      p = skip_ws(p);
    } else if(*p != '}') {
      return -1;
    }
  }

  if(*p != '}')
    return -1;
  if(pg->ntok == 0) {
    pg->toks[0].kind = AWK_TOK_DOLLAR0;
    pg->ntok = 1;
  }
  return 0;
}

static int
split_ws(char *line, char *fields[], int maxf)
{
  int nf;
  char *p;

  nf = 0;
  p = line;
  while(*p) {
    while(*p == ' ' || *p == '\t' || *p == '\n' || *p == '\r')
      *p++ = '\0';
    if(*p == '\0')
      break;
    if(nf < maxf)
      fields[nf++] = p;
    while(*p && *p != ' ' && *p != '\t' && *p != '\n' && *p != '\r')
      p++;
  }
  return nf;
}

static int
split_fs(char *line, const char *fs, char *fields[], int maxf)
{
  int nf;
  int flen;
  char *p;
  char *m;

  if(fs == 0 || fs[0] == '\0' || (fs[0] == ' ' && fs[1] == '\0'))
    return split_ws(line, fields, maxf);

  flen = strlen(fs);
  nf = 0;
  p = line;

  while(1) {
    if(nf < maxf)
      fields[nf++] = p;
    m = strstr(p, fs);
    if(m == 0)
      break;
    memset(m, 0, flen);
    p = m + flen;
  }

  return nf;
}

static int
run_stream(struct awk_prog *pg, FILE *fp, unsigned long *nr)
{
  char line[AWK_LINE_MAX];

  while(fgets(line, sizeof(line), fp) != 0) {
    char line_fields[AWK_LINE_MAX];
    char *fields[AWK_MAX_FIELDS];
    int nf;
    int i;

    (*nr)++;

    if(pg->has_pattern && regexec(&pg->pattern, line, 0, 0, 0) != 0)
      continue;

    strcpy(line_fields, line);
    nf = split_fs(line_fields, pg->fs, fields, AWK_MAX_FIELDS);

    for(i = 0; i < pg->ntok; i++) {
      struct awk_token *tk;

      tk = &pg->toks[i];
      if(i > 0)
        fputc(' ', stdout);

      if(tk->kind == AWK_TOK_DOLLAR0) {
        fputs(line, stdout);
        if(strlen(line) > 0 && line[strlen(line) - 1] != '\n')
          fputc('\n', stdout);
        break;
      }
      if(tk->kind == AWK_TOK_NR) {
        fprintf(stdout, "%lu", *nr);
        continue;
      }
      if(tk->kind == AWK_TOK_STRING) {
        fputs(tk->text, stdout);
        continue;
      }
      if(tk->kind == AWK_TOK_FIELD) {
        int idx;

        idx = tk->field_no - 1;
        if(idx >= 0 && idx < nf)
          fputs(fields[idx], stdout);
      }
    }

    if(pg->ntok > 0 && pg->toks[pg->ntok - 1].kind != AWK_TOK_DOLLAR0)
      fputc('\n', stdout);
  }

  return 0;
}

int
main(int argc, char *argv[])
{
  struct awk_prog pg;
  const char *fs;
  unsigned long nr;
  int argi;
  int i;

  nr = 0;
  argi = 1;
  fs = " ";

  if(argc < 2)
    usage();

  while(argi < argc && argv[argi][0] == '-') {
    if(strcmp(argv[argi], "-F") == 0) {
      if(argi + 1 >= argc)
        usage();
      fs = argv[argi + 1];
      argi += 2;
      continue;
    }
    break;
  }

  if(argi >= argc)
    usage();

  if(parse_program(&pg, argv[argi]) < 0) {
    dprintf(2, "awk: invalid program: %s\n", argv[argi]);
    exit(1);
  }
  pg.fs = fs;
  argi++;

  if(argi >= argc) {
    if(run_stream(&pg, stdin, &nr) < 0)
      exit(1);
    exit(0);
  }

  for(i = argi; i < argc; i++) {
    FILE *fp;

    fp = fopen(argv[i], "r");
    if(fp == 0) {
      dprintf(2, "awk: cannot open %s\n", argv[i]);
      exit(1);
    }
    if(run_stream(&pg, fp, &nr) < 0) {
      fclose(fp);
      exit(1);
    }
    fclose(fp);
  }

  exit(0);
}
