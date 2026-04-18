#include "types.h"
#include "fcntl.h"
#include "param.h"
#include "auxv6/user.h"

#define MLFQ_TUNE_PATH "/proc/mlfq_tune"

static void
usage(void)
{
  dprintf(2,
      "usage:\n"
      "  mlfqtune\n"
      "  mlfqtune <ticks>\n"
      "  mlfqtune -m <milliseconds>\n");
  dprintf(2, "range: %d..%d ticks\n",
          MLFQ_BOOST_INTERVAL_MIN, MLFQ_BOOST_INTERVAL_MAX);
  exit(1);
}

static int
parse_u32(const char *s, uint *out)
{
  uint v;
  int i;

  if(s == 0 || s[0] == 0)
    return -1;

  v = 0;
  for(i = 0; s[i]; i++) {
    char c;
    c = s[i];
    if(c < '0' || c > '9')
      return -1;
    if(v > 100000000)
      return -1;
    v = v * 10 + (uint)(c - '0');
  }

  *out = v;
  return 0;
}

static int
show_current(void)
{
  int fd;
  int n;
  char buf[256];

  fd = open(MLFQ_TUNE_PATH, O_RDONLY);
  if(fd < 0) {
    dprintf(2, "mlfqtune: cannot open %s\n", MLFQ_TUNE_PATH);
    return -1;
  }

  n = read(fd, buf, sizeof(buf) - 1);
  close(fd);
  if(n < 0) {
    dprintf(2, "mlfqtune: cannot read %s\n", MLFQ_TUNE_PATH);
    return -1;
  }

  buf[n] = 0;
  dprintf(1, "%s", buf);
  return 0;
}

static int
set_ticks(uint ticks)
{
  int fd;

  if(ticks < MLFQ_BOOST_INTERVAL_MIN || ticks > MLFQ_BOOST_INTERVAL_MAX) {
    dprintf(2, "mlfqtune: ticks out of range (%d..%d)\n",
            MLFQ_BOOST_INTERVAL_MIN, MLFQ_BOOST_INTERVAL_MAX);
    return -1;
  }

  fd = open(MLFQ_TUNE_PATH, O_WRONLY);
  if(fd < 0) {
    dprintf(2, "mlfqtune: cannot open %s for write\n", MLFQ_TUNE_PATH);
    return -1;
  }

  if(dprintf(fd, "%d\n", ticks) < 0) {
    close(fd);
    dprintf(2, "mlfqtune: write failed\n");
    return -1;
  }

  close(fd);
  dprintf(1, "mlfqtune: set boost interval to %d ticks (~%d ms)\n",
          ticks, ticks * 10);
  return 0;
}

int
main(int argc, char **argv)
{
  uint v;

  if(argc == 1)
    return show_current() < 0;

  if(argc == 2) {
    if(strcmp(argv[1], "-h") == 0 || strcmp(argv[1], "--help") == 0)
      usage();
    if(parse_u32(argv[1], &v) < 0)
      usage();
    if(set_ticks(v) < 0)
      return 1;
    return show_current() < 0;
  }

  if(argc == 3 && strcmp(argv[1], "-m") == 0) {
    if(parse_u32(argv[2], &v) < 0)
      usage();
    if(v == 0)
      usage();
    v = (v + 9) / 10;
    if(set_ticks(v) < 0)
      return 1;
    return show_current() < 0;
  }

  usage();
  return 1;
}
