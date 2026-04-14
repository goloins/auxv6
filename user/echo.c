#include "types.h"
#include "sys/stat.h"
#include "auxv6/user.h"

/*
 * Process a single escape sequence starting after the backslash.
 * Writes the decoded byte into *out and returns the number of input
 * characters consumed (not counting the leading backslash).
 */
static int
proc_escape(const char *s, char *out)
{
  switch(*s) {
  case 'a':  *out = '\a'; return 1;
  case 'b':  *out = '\b'; return 1;
  case 'f':  *out = '\f'; return 1;
  case 'n':  *out = '\n'; return 1;
  case 'r':  *out = '\r'; return 1;
  case 't':  *out = '\t'; return 1;
  case 'v':  *out = '\v'; return 1;
  case '\\': *out = '\\'; return 1;
  case '0': {
    /* Octal: \0NNN */
    int val;
    int consumed;

    val = 0;
    consumed = 1;   /* the '0' itself */
    s++;
    while(consumed < 4 && *s >= '0' && *s <= '7') {
      val = (val << 3) + (*s - '0');
      s++;
      consumed++;
    }
    *out = (char)val;
    return consumed;
  }
  default:
    /* Unknown escape: emit the backslash literally, consume nothing */
    *out = '\\';
    return 0;
  }
}

int
main(int argc, char *argv[])
{
  int no_newline;
  int do_escape;
  int i;
  int first;

  no_newline = 0;
  do_escape  = 0;

  /*
   * Parse leading -n / -e / -ne / -en flags.
   * Stop as soon as we see an argument that isn't a flag.
   */
  for(i = 1; i < argc; i++) {
    const char *a = argv[i];
    const char *f;
    int is_flag;

    if(a[0] != '-' || a[1] == '\0')
      break;

    is_flag = 1;
    for(f = a + 1; *f; f++) {
      if(*f == 'n')      no_newline = 1;
      else if(*f == 'e') do_escape  = 1;
      else { is_flag = 0; break; }
    }
    if(!is_flag)
      break;
  }

  /* Print remaining arguments */
  first = 1;
  for(; i < argc; i++) {
    const char *s;

    if(!first)
      write(1, " ", 1);
    first = 0;

    if(!do_escape) {
      write(1, argv[i], strlen(argv[i]));
    } else {
      for(s = argv[i]; *s; ) {
        if(*s == '\\') {
          char decoded;
          int consumed;

          consumed = proc_escape(s + 1, &decoded);
          write(1, &decoded, 1);
          s += 1 + consumed;
        } else {
          write(1, s, 1);
          s++;
        }
      }
    }
  }

  if(!no_newline)
    write(1, "\n", 1);

  exit(0);
}

