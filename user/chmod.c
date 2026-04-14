#include "types.h"
#include "sys/stat.h"
#include "auxv6/user.h"

static int
parse_octal_mode(const char *s)
{
  int mode;

  if(s == 0 || *s == 0)
    return -1;

  mode = 0;
  while(*s){
    if(*s < '0' || *s > '7')
      return -1;
    mode = (mode << 3) + (*s - '0');
    s++;
  }
  return mode;
}

/*
 * Apply one symbolic mode clause (e.g. "u+x", "go-w", "a=rx")
 * to current_mode.  Advances *sp past the clause.
 * is_dir: non-zero when the target is a directory (for 'X').
 * Returns updated permission bits (low 12 bits), or -1 on parse error.
 */
static int
apply_clause(int current_mode, int is_dir, const char **sp)
{
  const char *s = *sp;
  int who = 0;
  char op;
  int u_add = 0, g_add = 0, o_add = 0;
  int perm_mask, clear_mask, new_mode;

  /* Parse who letters ([ugoa]*) */
  for(;;){
    if(*s == 'u')      { who |= 1; s++; }
    else if(*s == 'g') { who |= 2; s++; }
    else if(*s == 'o') { who |= 4; s++; }
    else if(*s == 'a') { who |= 7; s++; }
    else break;
  }
  if(who == 0) who = 7;  /* default: all */

  /* Parse op */
  if(*s != '+' && *s != '-' && *s != '=')
    return -1;
  op = *s++;

  /* Parse perm letters ([rwxXst]*) until comma, end, or unknown char */
  while(*s && *s != ','){
    switch(*s){
    case 'r': u_add |= M_IRUSR; g_add |= M_IRGRP; o_add |= M_IROTH; break;
    case 'w': u_add |= M_IWUSR; g_add |= M_IWGRP; o_add |= M_IWOTH; break;
    case 'x': u_add |= M_IXUSR; g_add |= M_IXGRP; o_add |= M_IXOTH; break;
    case 'X':
      /* Conditional execute: only if directory or already executable */
      if(is_dir || (current_mode & (M_IXUSR|M_IXGRP|M_IXOTH))){
        u_add |= M_IXUSR; g_add |= M_IXGRP; o_add |= M_IXOTH;
      }
      break;
    case 's':
      if(who & 1) u_add |= M_ISUID;
      if(who & 2) g_add |= M_ISGID;
      break;
    case 't': o_add |= M_ISVTX; break;
    default: goto done_perm;
    }
    s++;
  }
done_perm:;

  /* Build mask from requested who */
  perm_mask = 0;
  if(who & 1) perm_mask |= u_add;
  if(who & 2) perm_mask |= g_add;
  if(who & 4) perm_mask |= o_add;

  /* clear_mask covers all bits owned by the specified who (for '=') */
  clear_mask = 0;
  if(who & 1) clear_mask |= (M_IRUSR|M_IWUSR|M_IXUSR|M_ISUID);
  if(who & 2) clear_mask |= (M_IRGRP|M_IWGRP|M_IXGRP|M_ISGID);
  if(who & 4) clear_mask |= (M_IROTH|M_IWOTH|M_IXOTH|M_ISVTX);

  new_mode = current_mode & 07777;
  switch(op){
  case '+': new_mode |= perm_mask; break;
  case '-': new_mode &= ~perm_mask; break;
  case '=': new_mode = (new_mode & ~clear_mask) | perm_mask; break;
  }

  *sp = s;
  return new_mode;
}

/*
 * Apply a full symbolic mode string (comma-separated clauses) to current_mode.
 * Returns the new permission bits, or -1 on error.
 */
static int
apply_symbolic(int current_mode, int is_dir, const char *s)
{
  int mode = current_mode;

  for(;;){
    mode = apply_clause(mode, is_dir, &s);
    if(mode < 0) return -1;
    if(*s == ',') { s++; continue; }
    if(*s == '\0') break;
    return -1;  /* unexpected character after clause */
  }
  return mode;
}

int
main(int argc, char *argv[])
{
  int i;
  int status;
  const char *modestr;
  int is_symbolic;

  status = 0;

  if(argc < 3){
    dprintf(2, "usage: chmod mode file...\n");
    exit(1);
  }

  modestr = argv[1];

  /* Symbolic if first char is not an octal digit */
  is_symbolic = (*modestr < '0' || *modestr > '7');

  if(!is_symbolic){
    int mode = parse_octal_mode(modestr);
    if(mode < 0){
      dprintf(2, "chmod: invalid mode %s\n", modestr);
      exit(1);
    }
    for(i = 2; i < argc; i++){
      if(chmod(argv[i], mode) < 0){
        dprintf(2, "chmod: %s failed\n", argv[i]);
        status = 1;
      }
    }
  } else {
    /* Symbolic mode: stat each file to get current mode, then apply */
    for(i = 2; i < argc; i++){
      struct stat st;
      int new_mode;

      if(stat(argv[i], &st) < 0){
        dprintf(2, "chmod: cannot stat %s\n", argv[i]);
        status = 1;
        continue;
      }
      new_mode = apply_symbolic(st.st_mode & 07777, S_ISDIR(st.st_mode), modestr);
      if(new_mode < 0){
        dprintf(2, "chmod: invalid mode %s\n", modestr);
        exit(1);
      }
      if(chmod(argv[i], new_mode) < 0){
        dprintf(2, "chmod: %s failed\n", argv[i]);
        status = 1;
      }
    }
  }

  exit(status);
}