#include "auxv6/user.h"
#include "stdio.h"
#include "stdlib.h"
#include "string.h"

static int flag_count;
static int flag_dup;
static int flag_unique;

static void
emit_line(const char *line, int count)
{
  if(flag_dup && count < 2)
    return;
  if(flag_unique && count != 1)
    return;

  if(flag_count)
    dprintf(1, "%7d %s", count, line);
  else
    fputs(line, stdout);
}

int
main(int argc, char *argv[])
{
  FILE *fp;
  char *line;
  size_t line_cap;
  ssize_t got;
  char *prev;
  int count;
  int i;

  fp = stdin;
  i = 1;
  while(i < argc && argv[i][0] == '-') {
    if(strcmp(argv[i], "-c") == 0)
      flag_count = 1;
    else if(strcmp(argv[i], "-d") == 0)
      flag_dup = 1;
    else if(strcmp(argv[i], "-u") == 0)
      flag_unique = 1;
    else if(strcmp(argv[i], "--") == 0) {
      i++;
      break;
    } else {
      dprintf(2, "usage: uniq [-cdu] [file]\n");
      return 1;
    }
    i++;
  }

  if(i < argc) {
    fp = fopen(argv[i], "r");
    if(fp == 0) {
      dprintf(2, "uniq: %s: cannot open\n", argv[i]);
      return 1;
    }
  }

  line = 0;
  line_cap = 0;
  prev = 0;
  count = 0;

  while((got = getline(&line, &line_cap, fp)) >= 0) {
    char *cur;

    cur = (char*)malloc((size_t)got + 1);
    if(cur == 0) {
      dprintf(2, "uniq: out of memory\n");
      free(line);
      free(prev);
      if(fp != stdin)
        fclose(fp);
      return 1;
    }
    memmove(cur, line, (size_t)got + 1);

    if(prev == 0) {
      prev = cur;
      count = 1;
    } else if(strcmp(prev, cur) == 0) {
      count++;
      free(cur);
    } else {
      emit_line(prev, count);
      free(prev);
      prev = cur;
      count = 1;
    }
  }

  if(prev) {
    emit_line(prev, count);
    free(prev);
  }

  free(line);
  if(fp != stdin)
    fclose(fp);
  return 0;
}
