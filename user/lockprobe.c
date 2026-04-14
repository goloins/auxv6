#include "types.h"
#include "sys/stat.h"
#include "auxv6/user.h"
#include "fcntl.h"
#include "termios.h"
#include "sys/ioctl.h"
#include "stdio.h"

struct cfg {
  int loops;
  int workers;
  int timed_reads;
  int verbose;
  int debug;
  int handoff_selftest;
  int no_console;
  int no_fds;
};

static int g_verbose;
static int g_debug;

#define VLOG(...) do { if(g_verbose) dprintf(1, __VA_ARGS__); } while(0)
#define DLOG(...) do { if(g_debug) dprintf(1, __VA_ARGS__); } while(0)

static void
usage(void)
{
  dprintf(2,
          "usage: lockprobe [-v] [-D] [-L] [-l loops] [-w workers] [-r timed_reads] [-C] [-F]\n"
          "  -v  verbose progress output\n"
          "  -D  debug output (very chatty, implies -v)\n"
          "  -L  run lockdep handoff selftest (pipe/ticks sleep transitions)\n"
          "  -l  loop count for fd/ioctl/write tests (default: 200)\n"
          "  -w  worker processes for concurrent fd stress (default: 4)\n"
          "  -r  timed tty read attempts in noncanonical mode (default: 32)\n"
          "  -C  skip console tests\n"
          "  -F  skip fd-table tests\n");
}

static int
parse_int_arg(const char *arg, const char *optname)
{
  int v;

  if(arg == 0) {
    dprintf(2, "lockprobe: missing value for %s\n", optname);
    return -1;
  }
  v = atoi(arg);
  if(v < 0) {
    dprintf(2, "lockprobe: invalid value '%s' for %s\n", arg, optname);
    return -1;
  }
  return v;
}

static int
worker_fd_stress(int loops, int wid)
{
  int i;
  int fd;
  int dupfd;
  int tmpfd;
  int r;
  char buf[64];
  char tmp[64];

  for(i = 0; i < loops; i++) {
    fd = open("/etc/passwd", O_RDONLY);
    if(fd < 0)
      return -1;

    r = read(fd, buf, sizeof(buf));
    if(r < 0) {
      close(fd);
      return -1;
    }

    dupfd = dup(fd);
    if(dupfd < 0) {
      close(fd);
      return -1;
    }

    if(close(dupfd) < 0) {
      close(fd);
      return -1;
    }

    if(close(fd) < 0)
      return -1;

    // Touch create/truncate/write/close paths repeatedly.
    tmpfd = open("/tmp/lockprobe.tmp", O_CREATE | O_TRUNC | O_RDWR);
    if(tmpfd < 0)
      return -1;

    r = snprintf(tmp, sizeof(tmp), "worker=%d iter=%d\n", wid, i);
    if(r < 0 || write(tmpfd, tmp, r) != r) {
      close(tmpfd);
      return -1;
    }

    if(close(tmpfd) < 0)
      return -1;

    if(g_debug && (i % 50) == 0)
      DLOG("lockprobe: worker %d iter %d ok\n", wid, i);
  }

  return 0;
}

static int
test_ftable_paths(const struct cfg *c)
{
  int i;
  int st;
  int pid;
  int ok;

  VLOG("lockprobe: [ftable] start loops=%d workers=%d\n", c->loops, c->workers);

  // Single-thread fast-path stress.
  if(worker_fd_stress(c->loops, 0) < 0) {
    dprintf(2, "lockprobe: [ftable] single-thread stress failed\n");
    return -1;
  }

  // Multi-process contention stress.
  ok = 1;
  for(i = 0; i < c->workers; i++) {
    pid = fork();
    if(pid < 0) {
      dprintf(2, "lockprobe: [ftable] fork failed at worker %d\n", i);
      ok = 0;
      break;
    }
    if(pid == 0) {
      int rc;
      rc = worker_fd_stress(c->loops, i + 1);
      exit(rc == 0 ? 0 : 1);
    }
  }

  for(i = 0; i < c->workers; i++) {
    pid = waitpid(-1, &st, 0);
    if(pid < 0) {
      ok = 0;
      break;
    }
    if(st != 0) {
      dprintf(2, "lockprobe: [ftable] worker pid=%d exit=%d\n", pid, st);
      ok = 0;
    }
  }

  if(unlink("/tmp/lockprobe.tmp") < 0)
    DLOG("lockprobe: [ftable] cleanup unlink /tmp/lockprobe.tmp failed (non-fatal)\n");

  if(!ok)
    return -1;

  VLOG("lockprobe: [ftable] pass\n");
  return 0;
}

static int
test_console_ioctl_write(const struct cfg *c)
{
  int i;
  int ttyfd;
  struct termios tio;
  struct winsize ws;
  int r;

  VLOG("lockprobe: [console] start loops=%d timed_reads=%d\n", c->loops, c->timed_reads);

  ttyfd = open("/dev/console", O_RDWR);
  if(ttyfd < 0) {
    dprintf(2, "lockprobe: [console] open /dev/console failed\n");
    return -1;
  }

  for(i = 0; i < c->loops; i++) {
    if(tcgetattr(ttyfd, &tio) < 0) {
      close(ttyfd);
      dprintf(2, "lockprobe: [console] tcgetattr failed iter=%d\n", i);
      return -1;
    }

    if(tcsetattr(ttyfd, TCSANOW, &tio) < 0) {
      close(ttyfd);
      dprintf(2, "lockprobe: [console] tcsetattr failed iter=%d\n", i);
      return -1;
    }

    if(ioctl(ttyfd, TIOCGWINSZ, &ws) < 0) {
      close(ttyfd);
      dprintf(2, "lockprobe: [console] TIOCGWINSZ failed iter=%d\n", i);
      return -1;
    }

    if((i % 16) == 0) {
      static const char msg[] = "lockprobe: console write path\n";
      r = write(ttyfd, msg, sizeof(msg) - 1);
      if(r != (int)(sizeof(msg) - 1)) {
        close(ttyfd);
        dprintf(2, "lockprobe: [console] write failed iter=%d\n", i);
        return -1;
      }
    }
  }

  close(ttyfd);
  VLOG("lockprobe: [console] ioctl/write pass\n");
  return 0;
}

static int
test_console_timed_reads(const struct cfg *c)
{
  struct termios oldt;
  struct termios t;
  int i;
  int r;
  char ch;
  int got;

  if(!isatty(0)) {
    VLOG("lockprobe: [console] stdin is not a tty; skipping timed read test\n");
    return 0;
  }

  if(tcgetattr(0, &oldt) < 0) {
    dprintf(2, "lockprobe: [console] tcgetattr(stdin) failed\n");
    return -1;
  }

  t = oldt;
  t.c_lflag &= ~(ICANON | ECHO);
  t.c_cc[VMIN] = 0;
  t.c_cc[VTIME] = 1; // 100ms timeout in noncanonical mode.

  if(tcsetattr(0, TCSANOW, &t) < 0) {
    dprintf(2, "lockprobe: [console] tcsetattr(stdin, raw timeout) failed\n");
    return -1;
  }

  got = 0;
  for(i = 0; i < c->timed_reads; i++) {
    r = read(0, &ch, 1);
    if(r < 0) {
      tcsetattr(0, TCSANOW, &oldt);
      dprintf(2, "lockprobe: [console] timed read failed iter=%d\n", i);
      return -1;
    }
    if(r == 1)
      got++;
    if(g_debug)
      DLOG("lockprobe: [console] timed read iter=%d r=%d\n", i, r);
  }

  if(tcsetattr(0, TCSANOW, &oldt) < 0) {
    dprintf(2, "lockprobe: [console] restore tcsetattr(stdin) failed\n");
    return -1;
  }

  VLOG("lockprobe: [console] timed read pass (bytes=%d/%d)\n", got, c->timed_reads);
  return 0;
}

static int
test_lockdep_handoffs(const struct cfg *c)
{
  int p[2];
  int pid;
  int st;
  int i;
  int n;
  int total;
  char wbuf[128];
  char rbuf[128];

  (void)c;
  VLOG("lockprobe: [handoff] start (pipe + ticks sleep)\n");

  if(pipe(p) < 0) {
    dprintf(2, "lockprobe: [handoff] pipe failed\n");
    return -1;
  }

  pid = fork();
  if(pid < 0) {
    close(p[0]);
    close(p[1]);
    dprintf(2, "lockprobe: [handoff] fork failed\n");
    return -1;
  }

  if(pid == 0) {
    close(p[0]);
    memset(wbuf, 'A', sizeof(wbuf));
    for(i = 0; i < 64; i++) {
      if(write(p[1], wbuf, sizeof(wbuf)) != (int)sizeof(wbuf)) {
        close(p[1]);
        exit(1);
      }
    }
    close(p[1]);
    exit(0);
  }

  close(p[1]);
  total = 0;
  for(i = 0; i < 64; i++) {
    n = read(p[0], rbuf, sizeof(rbuf));
    if(n <= 0) {
      close(p[0]);
      waitpid(pid, &st, 0);
      dprintf(2, "lockprobe: [handoff] pipe read failed at iter=%d\n", i);
      return -1;
    }
    total += n;
    if((i % 4) == 0)
      sleep(1);
  }
  close(p[0]);

  if(waitpid(pid, &st, 0) < 0 || st != 0) {
    dprintf(2, "lockprobe: [handoff] writer exit=%d\n", st);
    return -1;
  }

  for(i = 0; i < 8; i++)
    sleep(1);

  VLOG("lockprobe: [handoff] pass (bytes=%d)\n", total);
  return 0;
}

int
main(int argc, char **argv)
{
  struct cfg c;
  int i;
  int fails;

  c.loops = 200;
  c.workers = 4;
  c.timed_reads = 32;
  c.verbose = 0;
  c.debug = 0;
  c.handoff_selftest = 0;
  c.no_console = 0;
  c.no_fds = 0;

  for(i = 1; i < argc; i++) {
    if(strcmp(argv[i], "-h") == 0 || strcmp(argv[i], "--help") == 0) {
      usage();
      exit(0);
    } else if(strcmp(argv[i], "-v") == 0) {
      c.verbose = 1;
    } else if(strcmp(argv[i], "-D") == 0) {
      c.debug = 1;
      c.verbose = 1;
    } else if(strcmp(argv[i], "-L") == 0) {
      c.handoff_selftest = 1;
    } else if(strcmp(argv[i], "-C") == 0) {
      c.no_console = 1;
    } else if(strcmp(argv[i], "-F") == 0) {
      c.no_fds = 1;
    } else if(strcmp(argv[i], "-l") == 0) {
      c.loops = parse_int_arg((i + 1) < argc ? argv[++i] : 0, "-l");
      if(c.loops < 0)
        exit(1);
    } else if(strcmp(argv[i], "-w") == 0) {
      c.workers = parse_int_arg((i + 1) < argc ? argv[++i] : 0, "-w");
      if(c.workers < 0)
        exit(1);
    } else if(strcmp(argv[i], "-r") == 0) {
      c.timed_reads = parse_int_arg((i + 1) < argc ? argv[++i] : 0, "-r");
      if(c.timed_reads < 0)
        exit(1);
    } else {
      dprintf(2, "lockprobe: unknown option %s\n", argv[i]);
      usage();
      exit(1);
    }
  }

  if(c.loops == 0)
    c.loops = 1;
  if(c.workers == 0)
    c.workers = 1;

  g_verbose = c.verbose;
  g_debug = c.debug;

    dprintf(1, "lockprobe: begin loops=%d workers=%d timed_reads=%d verbose=%d debug=%d handoff=%d\n",
      c.loops, c.workers, c.timed_reads, c.verbose, c.debug, c.handoff_selftest);

  fails = 0;

  if(!c.no_fds) {
    if(test_ftable_paths(&c) < 0)
      fails++;
  } else {
    VLOG("lockprobe: skipping fd-table tests (-F)\n");
  }

  if(!c.no_console) {
    if(test_console_ioctl_write(&c) < 0)
      fails++;
    if(test_console_timed_reads(&c) < 0)
      fails++;
  } else {
    VLOG("lockprobe: skipping console tests (-C)\n");
  }

  if(c.handoff_selftest) {
    if(test_lockdep_handoffs(&c) < 0)
      fails++;
  } else {
    VLOG("lockprobe: skipping handoff selftest (enable with -L)\n");
  }

  if(fails == 0) {
    dprintf(1, "lockprobe: PASS\n");
    exit(0);
  }

  dprintf(1, "lockprobe: FAIL (%d checks failed)\n", fails);
  exit(1);
}
