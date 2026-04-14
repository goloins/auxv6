#include "types.h"
#include "sys/stat.h"
#include "auxv6/user.h"

#define F_LINES  0x1
#define F_WORDS  0x2
#define F_BYTES  0x4
#define F_CHARS  0x8   /* -m: same as -c on this system (single-byte) */
#define F_ALL    (F_LINES | F_WORDS | F_BYTES)

static char buf[512];

struct wc_counts {
  int lines;
  int words;
  int bytes;
};

static void
wc_fd(int fd, struct wc_counts *out)
{
  int i;
  int n;
  int inword;

  out->lines = 0;
  out->words = 0;
  out->bytes = 0;
  inword = 0;

  while((n = read(fd, buf, sizeof(buf))) > 0) {
    out->bytes += n;
    for(i = 0; i < n; i++) {
      if(buf[i] == '\n')
        out->lines++;
      if(strchr(" \r\t\n\v", buf[i]))
        inword = 0;
      else if(!inword) {
        out->words++;
        inword = 1;
      }
    }
  }
  if(n < 0) {
    dprintf(2, "wc: read error\n");
    exit(1);
  }
}

static void
print_counts(const struct wc_counts *c, const char *name, int flags)
{
  int first;

  first = 1;

#define SEP()  do { if(!first) dprintf(1, " "); first = 0; } while(0)

  if(flags & F_LINES) { SEP(); dprintf(1, "%d", c->lines); }
  if(flags & F_WORDS) { SEP(); dprintf(1, "%d", c->words); }
  if((flags & F_BYTES) || (flags & F_CHARS)) { SEP(); dprintf(1, "%d", c->bytes); }

#undef SEP

  if(name && name[0] != '\0')
    dprintf(1, " %s", name);
  dprintf(1, "\n");
}

int
main(int argc, char *argv[])
{
  int flags;
  int i;
  int fd;
  struct wc_counts c;
  struct wc_counts total;
  int nfiles;
  int explicit_flags;

  flags          = 0;
  explicit_flags = 0;
  total.lines    = 0;
  total.words    = 0;
  total.bytes    = 0;

  /* Parse flags */
  for(i = 1; i < argc && argv[i][0] == '-' && argv[i][1] != '\0'; i++) {
    char *f;

    for(f = argv[i] + 1; *f; f++) {
      switch(*f) {
      case 'l': flags |= F_LINES; explicit_flags = 1; break;
      case 'w': flags |= F_WORDS; explicit_flags = 1; break;
      case 'c': flags |= F_BYTES; explicit_flags = 1; break;
      case 'm': flags |= F_CHARS; explicit_flags = 1; break;
      default:
        dprintf(2, "wc: unknown option '-%c'\n", *f);
        dprintf(2, "usage: wc [-lwcm] [file...]\n");
        exit(1);
      }
    }
  }

  if(!explicit_flags)
    flags = F_ALL;

  nfiles = argc - i;

  if(nfiles == 0) {
    wc_fd(0, &c);
    print_counts(&c, "", flags);
    exit(0);
  }

  for(; i < argc; i++) {
    if((fd = open(argv[i], 0)) < 0) {
      dprintf(2, "wc: cannot open %s\n", argv[i]);
      exit(1);
    }
    wc_fd(fd, &c);
    close(fd);
    print_counts(&c, argv[i], flags);
    total.lines += c.lines;
    total.words += c.words;
    total.bytes += c.bytes;
  }

  if(nfiles > 1)
    print_counts(&total, "total", flags);

  exit(0);
}

