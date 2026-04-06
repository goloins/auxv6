#include "auxv6/user.h"
#include "stdio.h"
#include "stdlib.h"
#include "string.h"

struct sort_line {
  char *s;
};

static int sort_numeric;
static int sort_reverse;

static int
line_cmp(const void *a, const void *b)
{
  const struct sort_line *la = (const struct sort_line*)a;
  const struct sort_line *lb = (const struct sort_line*)b;
  int cmp;

  if(sort_numeric) {
    long va, vb;
    char *ea, *eb;

    va = strtol(la->s, &ea, 10);
    vb = strtol(lb->s, &eb, 10);
    if(va < vb)
      cmp = -1;
    else if(va > vb)
      cmp = 1;
    else
      cmp = strcmp(la->s, lb->s);
  } else {
    cmp = strcmp(la->s, lb->s);
  }

  return sort_reverse ? -cmp : cmp;
}

static int
add_line(struct sort_line **arr, int *n, int *cap, const char *line)
{
  size_t len;
  char *copy;

  if(*n >= *cap) {
    int ncap;
    struct sort_line *tmp;

    ncap = (*cap == 0) ? 128 : (*cap * 2);
    tmp = (struct sort_line*)realloc(*arr, ncap * sizeof(struct sort_line));
    if(tmp == 0)
      return -1;
    *arr = tmp;
    *cap = ncap;
  }

  len = strlen(line);
  copy = (char*)malloc(len + 1);
  if(copy == 0)
    return -1;
  memmove(copy, line, len + 1);

  (*arr)[*n].s = copy;
  (*n)++;
  return 0;
}

static int
read_lines(FILE *fp, struct sort_line **arr, int *n, int *cap)
{
  char *line;
  size_t line_cap;
  ssize_t got;

  line = 0;
  line_cap = 0;
  while((got = getline(&line, &line_cap, fp)) >= 0) {
    (void)got;
    if(add_line(arr, n, cap, line) < 0) {
      free(line);
      return -1;
    }
  }

  free(line);
  return 0;
}

int
main(int argc, char *argv[])
{
  struct sort_line *arr;
  int n, cap;
  int i;
  int rc;

  arr = 0;
  n = 0;
  cap = 0;
  rc = 0;

  i = 1;
  while(i < argc && argv[i][0] == '-') {
    if(strcmp(argv[i], "-r") == 0)
      sort_reverse = 1;
    else if(strcmp(argv[i], "-n") == 0)
      sort_numeric = 1;
    else if(strcmp(argv[i], "--") == 0) {
      i++;
      break;
    } else {
      dprintf(2, "usage: sort [-n] [-r] [file ...]\n");
      return 1;
    }
    i++;
  }

  if(i == argc) {
    if(read_lines(stdin, &arr, &n, &cap) < 0) {
      dprintf(2, "sort: out of memory\n");
      return 1;
    }
  } else {
    for(; i < argc; i++) {
      FILE *fp;
      if(strcmp(argv[i], "-") == 0)
        fp = stdin;
      else
        fp = fopen(argv[i], "r");

      if(fp == 0) {
        dprintf(2, "sort: %s: cannot open\n", argv[i]);
        rc = 1;
        continue;
      }
      if(read_lines(fp, &arr, &n, &cap) < 0) {
        dprintf(2, "sort: out of memory\n");
        if(fp != stdin)
          fclose(fp);
        rc = 1;
        break;
      }
      if(fp != stdin)
        fclose(fp);
    }
  }

  if(n > 1)
    qsort(arr, (uint)n, sizeof(struct sort_line), line_cmp);

  for(i = 0; i < n; i++) {
    fputs(arr[i].s, stdout);
    free(arr[i].s);
  }
  free(arr);

  return rc;
}
