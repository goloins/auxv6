#include "types.h"
#include "stat.h"
#include "auxv6/user.h"
#include "fcntl.h"
#include "string.h"

#define UNAME_RAW_MAX 128
#define HOST_MAX       64
#define FIELD_MAX      48

/* Flag bits */
#define UF_SYSNAME   0x01  /* -s: kernel/OS name */
#define UF_NODENAME  0x02  /* -n: hostname */
#define UF_RELEASE   0x04  /* -r: kernel release */
#define UF_VERSION   0x08  /* -v: kernel version (same as release here) */
#define UF_MACHINE   0x10  /* -m: machine hardware */
#define UF_ALL       (UF_SYSNAME|UF_NODENAME|UF_RELEASE|UF_VERSION|UF_MACHINE)

static void
trim(char *buf)
{
  int i;

  for(i = strlen(buf) - 1; i >= 0; i--) {
    if(buf[i] == '\n' || buf[i] == '\r' ||
       buf[i] == ' '  || buf[i] == '\t')
      buf[i] = '\0';
    else
      break;
  }
}

static int
load_hostname(char *buf, int bufsz)
{
  int fd;
  int n;

  fd = open("/etc/hostname", O_RDONLY);
  if(fd < 0)
    return -1;
  n = read(fd, buf, bufsz - 1);
  close(fd);
  if(n <= 0)
    return -1;
  buf[n] = '\0';
  trim(buf);
  return buf[0] ? 0 : -1;
}

/*
 * Split the raw uname string (e.g. "a/ux86 aux86 i686") by spaces into
 * sysname, release, and machine.
 */
static void
parse_uname(const char *raw, char *sysname, char *release, char *machine,
            int sz)
{
  const char *p;
  int i;
  char *fields[3];
  char tmp[UNAME_RAW_MAX];
  char *tok;

  fields[0] = sysname;
  fields[1] = release;
  fields[2] = machine;

  strncpy(tmp, raw, sizeof(tmp) - 1);
  tmp[sizeof(tmp) - 1] = '\0';

  tok = tmp;
  for(i = 0; i < 3; i++) {
    /* skip leading spaces */
    while(*tok == ' ') tok++;
    p = tok;
    while(*tok && *tok != ' ') tok++;
    if(*tok == ' ') *tok++ = '\0';

    if(*p == '\0')
      strncpy(fields[i], "unknown", sz);
    else
      strncpy(fields[i], p, sz - 1);
    fields[i][sz - 1] = '\0';
  }
}

static void
usage(void)
{
  dprintf(2, "usage: uname [-asnrmv]\n");
  exit(1);
}

int
main(int argc, char *argv[])
{
  char raw[UNAME_RAW_MAX];
  char sysname[FIELD_MAX];
  char release[FIELD_MAX];
  char machine[FIELD_MAX];
  char nodename[HOST_MAX];
  int flags;
  int i;
  int first;

  flags = 0;

  for(i = 1; i < argc; i++) {
    char *f;

    if(argv[i][0] != '-' || argv[i][1] == '\0') {
      usage();
    }
    for(f = argv[i] + 1; *f; f++) {
      switch(*f) {
      case 'a': flags |= UF_ALL;     break;
      case 's': flags |= UF_SYSNAME; break;
      case 'n': flags |= UF_NODENAME;break;
      case 'r': flags |= UF_RELEASE; break;
      case 'v': flags |= UF_VERSION; break;
      case 'm': flags |= UF_MACHINE; break;
      default:
        dprintf(2, "uname: unknown option '-%c'\n", *f);
        usage();
      }
    }
  }

  /* Default (no flags) behaves like -s */
  if(flags == 0)
    flags = UF_SYSNAME;

  /* Get raw string from kernel */
  if(uname(raw, sizeof(raw)) < 0) {
    dprintf(2, "uname: syscall failed\n");
    exit(1);
  }
  trim(raw);

  parse_uname(raw, sysname, release, machine, FIELD_MAX);

  /* Nodename from /etc/hostname */
  if(load_hostname(nodename, sizeof(nodename)) < 0)
    strncpy(nodename, "localhost", sizeof(nodename));

  /* Print selected fields in POSIX order */
  first = 1;

#define EMIT(str) do { \
    if(!first) write(1, " ", 1); \
    first = 0; \
    write(1, (str), strlen(str)); \
  } while(0)

  if(flags & UF_SYSNAME)  EMIT(sysname);
  if(flags & UF_NODENAME) EMIT(nodename);
  if(flags & UF_RELEASE)  EMIT(release);
  if(flags & UF_VERSION)  EMIT(release);  /* version == release on auxv6 */
  if(flags & UF_MACHINE)  EMIT(machine);

#undef EMIT

  write(1, "\n", 1);
  exit(0);
}
