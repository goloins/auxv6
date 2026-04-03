#include "types.h"
#include "auxv6/user.h"
#include "fcntl.h"
#include "termios.h"
#include "sys/ioctl.h"

static int collect_query_reply(int fd, const char *query, char *buf, int buflen);

#define PTY_SHELL_SESSIONS 3
#define PTY_POLL_LOOPS 40
#define PTY_STRESS_CAP 64
#define PTY_STRESS_CYCLES 8

struct pty_shell_session {
  int mfd;
  int pid;
  int seen;
  char marker[16];
};

static volatile int bg_sig_seen;
static int tc_log_fd = -1;
static int tc_debug_query = 0;

static void
tc_log_raw(const char *s)
{
  if(tc_log_fd < 0 || s == 0)
    return;
  (void)write(tc_log_fd, s, strlen(s));
}

static void
tc_log_line(const char *s)
{
  tc_log_raw(s);
  tc_log_raw("\n");
}

static void
tc_report(int outfd, const char *line)
{
  dprintf(outfd, "%s\n", line);
  tc_log_line(line);
}

static void
tc_report_failcount(int fails)
{
  char tmp[12];
  int n;

  dprintf(2, "termcheck: %d checks failed\n", fails);
  if(tc_log_fd < 0)
    return;

  tc_log_raw("termcheck: ");
  if(fails <= 0) {
    tc_log_raw("0");
  } else {
    n = 0;
    while(fails > 0 && n < (int)sizeof(tmp)) {
      tmp[n++] = (char)('0' + (fails % 10));
      fails /= 10;
    }
    while(n > 0) {
      char ch = tmp[--n];
      (void)write(tc_log_fd, &ch, 1);
    }
  }
  tc_log_line(" checks failed");
}

static void
termcheck_bg_sig_handler(int signo)
{
  if(signo == SIGTTIN || signo == SIGTTOU)
    bg_sig_seen = 1;
}

static int
contains_token(const char *buf, int n, const char *tok)
{
  int i;
  int j;
  int tlen;

  tlen = strlen(tok);
  if(tlen <= 0 || n < tlen)
    return 0;

  for(i = 0; i <= n - tlen; i++) {
    for(j = 0; j < tlen; j++) {
      if(buf[i + j] != tok[j])
        break;
    }
    if(j == tlen)
      return 1;
  }
  return 0;
}

static void
tc_debug_emit_escaped(int fd, const char *buf, int n)
{
  static const char hex[] = "0123456789abcdef";
  int i;

  if(buf == 0 || n <= 0)
    return;

  for(i = 0; i < n; i++) {
    unsigned char ch;
    ch = (unsigned char)buf[i];
    if(ch >= 32 && ch <= 126 && ch != '\\') {
      (void)write(fd, &buf[i], 1);
    } else {
      char esc[4];
      esc[0] = '\\';
      esc[1] = 'x';
      esc[2] = hex[(ch >> 4) & 0x0F];
      esc[3] = hex[ch & 0x0F];
      (void)write(fd, esc, 4);
    }
  }
}

static void
tc_debug_query_fail(const char *label, const char *query, const char *expected, const char *buf, int n)
{
  if(!tc_debug_query)
    return;

  dprintf(2, "DEBUG query fail: %s\n", label);
  dprintf(2, "  expected token: %s\n", expected);
  dprintf(2, "  query bytes: ");
  tc_debug_emit_escaped(2, query, strlen(query));
  dprintf(2, "\n");
  dprintf(2, "  reply bytes: ");
  tc_debug_emit_escaped(2, buf, n > 0 ? n : 0);
  dprintf(2, "\n");

  tc_log_line("DEBUG query fail");
  tc_log_raw("  label: ");
  tc_log_line(label);
  tc_log_raw("  expected token: ");
  tc_log_line(expected);
  if(tc_log_fd >= 0) {
    tc_log_raw("  query bytes: ");
    tc_debug_emit_escaped(tc_log_fd, query, strlen(query));
    tc_log_raw("\n");
    tc_log_raw("  reply bytes: ");
    tc_debug_emit_escaped(tc_log_fd, buf, n > 0 ? n : 0);
    tc_log_raw("\n");
  }
}

static int
expect_token_reply(int fd, const char *label, const char *query, const char *expected, char *buf, int buflen)
{
  int n;

  n = collect_query_reply(fd, query, buf, buflen);
  if(n < 0 || !contains_token(buf, n, expected)) {
    tc_debug_query_fail(label, query, expected, buf, n);
    return -1;
  }
  return n;
}

static void
build_marker(char *dst, int idx)
{
  dst[0] = 'T';
  dst[1] = 'C';
  dst[2] = 'P';
  dst[3] = 'T';
  dst[4] = 'Y';
  dst[5] = '_';
  dst[6] = '0' + idx;
  dst[7] = 0;
}

static int
spawn_shell_on_pty(struct pty_shell_session *sess, int idx)
{
  int pid;
  int sfd;
  char sname[32];
  char *sh_argv[] = { "sh", 0 };
  char *dash_argv[] = { "dash", 0 };

  memset(sess, 0, sizeof(*sess));
  sess->mfd = -1;
  sess->pid = -1;
  build_marker(sess->marker, idx);

  sess->mfd = open("/dev/ptmx", O_RDWR);
  if(sess->mfd < 0)
    return -1;

  if(ptsname_r(sess->mfd, sname, sizeof(sname)) < 0) {
    close(sess->mfd);
    sess->mfd = -1;
    return -1;
  }

  pid = fork();
  if(pid < 0) {
    close(sess->mfd);
    sess->mfd = -1;
    return -1;
  }

  if(pid == 0) {
    (void)setsid();
    (void)setpgid(0, 0);

    sfd = open(sname, O_RDWR);
    if(sfd < 0)
      exit(0);

    (void)ioctl(sfd, TIOCSCTTY, 0);

    close(0);
    dup(sfd);
    close(1);
    dup(sfd);
    close(2);
    dup(sfd);
    if(sfd > 2)
      close(sfd);

    exec("/bin/sh", sh_argv);
    exec("/sh", sh_argv);
    exec("/bin/dash", dash_argv);

    for(;;)
      sleep(1000);
  }

  sess->pid = pid;
  return 0;
}

static void
cleanup_shell_sessions(struct pty_shell_session *sess, int n)
{
  int i;
  int st;

  for(i = 0; i < n; i++) {
    if(sess[i].pid > 0)
      kill(sess[i].pid, SIGKILL);
  }
  for(i = 0; i < n; i++) {
    if(sess[i].pid > 0)
      waitpid(sess[i].pid, &st, 0);
    if(sess[i].mfd >= 0)
      close(sess[i].mfd);
    sess[i].pid = -1;
    sess[i].mfd = -1;
  }
}

static int
check_multi_pty_shells(void)
{
  struct pty_shell_session sess[PTY_SHELL_SESSIONS];
  char cmd[32];
  char out[128];
  int i;
  int j;
  int inq;
  int got;
  int all_seen;
  int loops;

  for(i = 0; i < PTY_SHELL_SESSIONS; i++) {
    if(spawn_shell_on_pty(&sess[i], i) < 0) {
      cleanup_shell_sessions(sess, PTY_SHELL_SESSIONS);
      return -1;
    }
    dprintf(1, "termcheck: spawned shell on PTY %d\n", i);
  }

  for(i = 0; i < PTY_SHELL_SESSIONS; i++) {
    memset(cmd, 0, sizeof(cmd));
    strcpy(cmd, "echo ");
    strcpy(cmd + 5, sess[i].marker);
    strcpy(cmd + 5 + strlen(sess[i].marker), "\n");
    if(write(sess[i].mfd, cmd, strlen(cmd)) < 0) {
      cleanup_shell_sessions(sess, PTY_SHELL_SESSIONS);
      return -1;
    }
  }

  for(loops = 0; loops < PTY_POLL_LOOPS; loops++) {
    if((loops % 8) == 0) {
      for(i = 0; i < PTY_SHELL_SESSIONS; i++) {
        if(sess[i].seen)
          continue;
        memset(cmd, 0, sizeof(cmd));
        strcpy(cmd, "echo ");
        strcpy(cmd + 5, sess[i].marker);
        strcpy(cmd + 5 + strlen(sess[i].marker), "\n");
        (void)write(sess[i].mfd, cmd, strlen(cmd));
      }
    }

    all_seen = 1;
    for(i = 0; i < PTY_SHELL_SESSIONS; i++) {
      if(!sess[i].seen)
        all_seen = 0;

      inq = 0;
      if(ioctl(sess[i].mfd, FIONREAD, &inq) < 0)
        continue;
      if(inq <= 0)
        continue;

      if(inq > (int)sizeof(out) - 1)
        inq = (int)sizeof(out) - 1;
      got = read(sess[i].mfd, out, inq);
      if(got <= 0)
        continue;
      out[got] = 0;

      if(contains_token(out, got, sess[i].marker)) {
        if(!sess[i].seen)
          dprintf(1, "termcheck: shell PTY %d produced marker %s\n", i, sess[i].marker);
        sess[i].seen = 1;
      }

      for(j = 0; j < PTY_SHELL_SESSIONS; j++) {
        if(j == i)
          continue;
        if(contains_token(out, got, sess[j].marker)) {
          cleanup_shell_sessions(sess, PTY_SHELL_SESSIONS);
          return -1;
        }
      }
    }

    all_seen = 1;
    for(i = 0; i < PTY_SHELL_SESSIONS; i++) {
      if(!sess[i].seen) {
        all_seen = 0;
        break;
      }
    }
    if(all_seen)
      break;

    sleep(1);
  }

  all_seen = 1;
  for(i = 0; i < PTY_SHELL_SESSIONS; i++) {
    if(!sess[i].seen) {
      all_seen = 0;
      break;
    }
  }

  cleanup_shell_sessions(sess, PTY_SHELL_SESSIONS);
  if(!all_seen)
    return -1;

  dprintf(1, "termcheck: all PTY shells terminated\n");
  return 0;
}

static int
open_pty_batch(int *mfds, int *sfds, int cap)
{
  int i;
  int mfd;
  int sfd;
  char sname[32];

  for(i = 0; i < cap; i++) {
    mfds[i] = -1;
    sfds[i] = -1;
  }

  for(i = 0; i < cap; i++) {
    mfd = open("/dev/ptmx", O_RDWR);
    if(mfd < 0)
      break;

    /*
     * For max-capacity stress we care about master allocation pressure.
     * Slave node availability can lag (for example stale rootfs /dev/pts set),
     * so open slave as best-effort only.
     */
    sfd = -1;
    if(ptsname_r(mfd, sname, sizeof(sname)) == 0)
      sfd = open(sname, O_RDWR);

    mfds[i] = mfd;
    sfds[i] = sfd;
  }

  return i;
}

static void
close_pty_batch(int *mfds, int *sfds, int n)
{
  int i;

  for(i = 0; i < n; i++) {
    if(sfds[i] >= 0)
      close(sfds[i]);
    if(mfds[i] >= 0)
      close(mfds[i]);
    sfds[i] = -1;
    mfds[i] = -1;
  }
}

static int
check_pty_max_stress(void)
{
  int mfds[PTY_STRESS_CAP];
  int sfds[PTY_STRESS_CAP];
  int count1;
  int count2;
  int best;
  int i;
  int extra;
  int slave_missing_seen;

  best = 0;
  slave_missing_seen = 0;
  for(i = 0; i < PTY_STRESS_CYCLES; i++) {
    count1 = open_pty_batch(mfds, sfds, PTY_STRESS_CAP);
    if(count1 <= 0) {
      close_pty_batch(mfds, sfds, count1);
      return -1;
    }

    if(count1 < best) {
      dprintf(2, "termcheck: pty stress regression cycle %d count=%d best=%d\n", i + 1, count1, best);
      close_pty_batch(mfds, sfds, count1);
      return -1;
    }
    if(count1 > best)
      best = count1;

    /* Note whether any slave opens failed; this is informative only. */
    {
      int k;
      for(k = 0; k < count1; k++) {
        if(sfds[k] < 0) {
          slave_missing_seen = 1;
          break;
        }
      }
    }

    extra = open("/dev/ptmx", O_RDWR);
    if(extra >= 0) {
      close(extra);
      close_pty_batch(mfds, sfds, count1);
      dprintf(2, "termcheck: pty stress expected ptmx allocation failure at count=%d\n", count1);
      return -1;
    }

    close_pty_batch(mfds, sfds, count1);

    /* Re-open immediately to detect teardown leaks in the same cycle. */
    count2 = open_pty_batch(mfds, sfds, PTY_STRESS_CAP);
    if(count2 < count1) {
      dprintf(2, "termcheck: pty stress leak? cycle %d count1=%d count2=%d\n", i + 1, count1, count2);
      close_pty_batch(mfds, sfds, count2);
      return -1;
    }
    if(count2 > best)
      best = count2;
    close_pty_batch(mfds, sfds, count2);

    dprintf(1, "termcheck: pty stress cycle %d/%d counts=%d,%d best=%d\n", i + 1, PTY_STRESS_CYCLES, count1, count2, best);
    sleep(1);
  }

  if(best < PTY_SHELL_SESSIONS)
    return -1;
  if(slave_missing_seen)
    dprintf(1, "termcheck: note: some /dev/pts/N slave nodes missing during max stress; master pressure test still valid\n");
  return 0;
}

static int
check_termios_roundtrip(int fd)
{
  struct termios oldt;
  struct termios t;
  struct termios got;

  if(tcgetattr(fd, &oldt) < 0)
    return -1;

  t = oldt;
  t.c_lflag &= ~(ECHO | ICANON);
  t.c_cc[VMIN] = 0;
  t.c_cc[VTIME] = 1;

  if(tcsetattr(fd, TCSANOW, &t) < 0)
    return -1;
  if(tcgetattr(fd, &got) < 0)
    return -1;

  if((got.c_lflag & (ECHO | ICANON)) != 0 ||
     got.c_cc[VMIN] != 0 ||
     got.c_cc[VTIME] != 1) {
    tcsetattr(fd, TCSANOW, &oldt);
    return -1;
  }

  tcsetattr(fd, TCSANOW, &oldt);
  return 0;
}

static int
check_noncanon_vmin0_vtime0(int fd)
{
  struct termios oldt;
  struct termios t;
  int mfd;
  int sfd;
  int use_fd;
  char sname[32];
  int inq;

  mfd = -1;
  sfd = -1;
  use_fd = fd;

  mfd = open("/dev/ptmx", O_RDWR);
  if(mfd >= 0) {
    if(ptsname_r(mfd, sname, sizeof(sname)) == 0)
      sfd = open(sname, O_RDWR);
    if(sfd >= 0)
      use_fd = sfd;
    else
      use_fd = mfd;
  }

  if(tcgetattr(use_fd, &oldt) < 0)
    return -1;

  t = oldt;
  t.c_lflag &= ~(ICANON | ECHO);
  t.c_cc[VMIN] = 0;
  t.c_cc[VTIME] = 0;

  if(tcsetattr(use_fd, TCSANOW, &t) < 0)
    return -1;

  (void)ioctl(use_fd, TCFLSH, TCIFLUSH);
  inq = 0;
  if(ioctl(use_fd, FIONREAD, &inq) < 0) {
    tcsetattr(use_fd, TCSANOW, &oldt);
    return -1;
  }

  tcsetattr(use_fd, TCSANOW, &oldt);

  if(sfd >= 0)
    close(sfd);
  if(mfd >= 0)
    close(mfd);

  if(inq != 0)
    return -1;
  return 0;
}

static void
drain_tty_input_nonblock(int fd)
{
  char tmp[64];
  int n;
  int loops;

  for(loops = 0; loops < 8; loops++) {
    n = read(fd, tmp, sizeof(tmp));
    if(n <= 0)
      break;
  }
}

static int
check_winsize_ioctl(int fd)
{
  struct winsize oldw;
  struct winsize testw;
  struct winsize gotw;

  if(ioctl(fd, TIOCGWINSZ, &oldw) < 0)
    return -1;

  testw = oldw;
  if(testw.ws_row < 30)
    testw.ws_row = 30;
  if(testw.ws_col < 100)
    testw.ws_col = 100;

  if(ioctl(fd, TIOCSWINSZ, &testw) < 0)
    return -1;
  if(ioctl(fd, TIOCGWINSZ, &gotw) < 0)
    return -1;

  (void)ioctl(fd, TIOCSWINSZ, &oldw);

  if(gotw.ws_row != testw.ws_row || gotw.ws_col != testw.ws_col)
    return -1;
  return 0;
}

static int
check_tcsetattr_optional_actions(int fd)
{
  struct termios oldt;
  struct termios t;
  struct termios got;

  if(tcgetattr(fd, &oldt) < 0)
    return -1;

  t = oldt;
  t.c_lflag ^= ECHO;
  if(tcsetattr(fd, TCSADRAIN, &t) < 0)
    return -1;
  if(tcgetattr(fd, &got) < 0)
    return -1;
  if((got.c_lflag & ECHO) != (t.c_lflag & ECHO)) {
    tcsetattr(fd, TCSANOW, &oldt);
    return -1;
  }

  t = oldt;
  t.c_lflag ^= ECHO;
  if(tcsetattr(fd, TCSAFLUSH, &t) < 0) {
    tcsetattr(fd, TCSANOW, &oldt);
    return -1;
  }
  if(tcgetattr(fd, &got) < 0) {
    tcsetattr(fd, TCSANOW, &oldt);
    return -1;
  }
  if((got.c_lflag & ECHO) != (t.c_lflag & ECHO)) {
    tcsetattr(fd, TCSANOW, &oldt);
    return -1;
  }

  tcsetattr(fd, TCSANOW, &oldt);
  return 0;
}

static int
check_ioctl_termios_compat(int fd)
{
  struct termios oldt;
  struct termios t;
  struct termios got;

  if(ioctl(fd, TCGETS, &oldt) < 0)
    return -1;

  t = oldt;
  t.c_lflag ^= ECHO;

  if(ioctl(fd, TCSETSW, &t) < 0)
    return -1;
  if(ioctl(fd, TCGETS, &got) < 0)
    return -1;
  if((got.c_lflag & ECHO) != (t.c_lflag & ECHO)) {
    ioctl(fd, TCSETS, &oldt);
    return -1;
  }

  if(ioctl(fd, TCSETSF, &oldt) < 0)
    return -1;
  if(ioctl(fd, TCGETS, &got) < 0)
    return -1;
  if((got.c_lflag & ECHO) != (oldt.c_lflag & ECHO))
    return -1;

  return 0;
}

static int
check_ioctl_queue_state(int fd)
{
  int inq;
  int outq;

  if(ioctl(fd, FIONREAD, &inq) < 0)
    return -1;
  if(ioctl(fd, TIOCOUTQ, &outq) < 0)
    return -1;
  if(inq < 0 || outq < 0)
    return -1;

  return 0;
}

static int
check_pty_roundtrip(void)
{
  int mfd;
  int sfd;
  char ch;
  char sname[32];

  mfd = open("/dev/ptmx", O_RDWR);
  if(mfd < 0)
    return -1;

  if(ptsname_r(mfd, sname, sizeof(sname)) < 0) {
    close(mfd);
    return -1;
  }

  sfd = open(sname, O_RDWR);
  if(sfd < 0) {
    close(mfd);
    return -1;
  }

  if(write(mfd, "M", 1) != 1) {
    close(sfd);
    close(mfd);
    return -1;
  }
  if(read(sfd, &ch, 1) != 1 || ch != 'M') {
    close(sfd);
    close(mfd);
    return -1;
  }

  if(write(sfd, "S", 1) != 1) {
    close(sfd);
    close(mfd);
    return -1;
  }
  if(read(mfd, &ch, 1) != 1 || ch != 'S') {
    close(sfd);
    close(mfd);
    return -1;
  }

  close(sfd);
  close(mfd);
  return 0;
}

static int
check_flag_roundtrip(int fd)
{
  struct termios oldt;
  struct termios t;
  struct termios got;

  if(tcgetattr(fd, &oldt) < 0)
    return -1;

  t = oldt;
  t.c_iflag ^= ISTRIP;
  t.c_lflag ^= ECHOCTL;

  if(tcsetattr(fd, TCSANOW, &t) < 0)
    return -1;
  if(tcgetattr(fd, &got) < 0) {
    tcsetattr(fd, TCSANOW, &oldt);
    return -1;
  }

  if((got.c_iflag & ISTRIP) != (t.c_iflag & ISTRIP) ||
     (got.c_lflag & ECHOCTL) != (t.c_lflag & ECHOCTL)) {
    tcsetattr(fd, TCSANOW, &oldt);
    return -1;
  }

  tcsetattr(fd, TCSANOW, &oldt);
  return 0;
}

static int
check_tty_identity_helpers(int fd)
{
  char *name;
  char buf[64];
  int mfd;
  int sfd;
  char sname[32];
  char *mname;
  char *slavename;

  name = ttyname(fd);
  if(name == 0)
    return -1;
  if(ttyname_r(fd, buf, sizeof(buf)) < 0)
    return -1;
  if(strcmp(name, buf) != 0)
    return -1;

  mfd = open("/dev/ptmx", O_RDWR);
  if(mfd < 0)
    return -1;
  if(!isatty(mfd)) {
    close(mfd);
    return -1;
  }

  mname = ttyname(mfd);
  if(mname == 0 || strcmp(mname, "/dev/ptmx") != 0) {
    close(mfd);
    return -1;
  }
  if(ttyname_r(mfd, buf, sizeof(buf)) < 0 || strcmp(buf, "/dev/ptmx") != 0) {
    close(mfd);
    return -1;
  }

  if(ptsname_r(mfd, sname, sizeof(sname)) < 0) {
    close(mfd);
    return -1;
  }
  sfd = open(sname, O_RDWR);
  if(sfd < 0) {
    close(mfd);
    return -1;
  }
  if(!isatty(sfd)) {
    close(sfd);
    close(mfd);
    return -1;
  }

  slavename = ttyname(sfd);
  if(slavename == 0 || strcmp(slavename, sname) != 0) {
    close(sfd);
    close(mfd);
    return -1;
  }
  if(ttyname_r(sfd, buf, sizeof(buf)) < 0 || strcmp(buf, sname) != 0) {
    close(sfd);
    close(mfd);
    return -1;
  }

  close(sfd);
  close(mfd);
  return 0;
}

static int
check_pty_pgrp_ioctl(void)
{
  int mfd;
  int sfd;
  int pg;
  int got;
  char sname[32];

  mfd = open("/dev/ptmx", O_RDWR);
  if(mfd < 0)
    return -1;
  if(ptsname_r(mfd, sname, sizeof(sname)) < 0) {
    close(mfd);
    return -1;
  }

  sfd = open(sname, O_RDWR);
  if(sfd < 0) {
    close(mfd);
    return -1;
  }

  if(ioctl(sfd, TIOCSCTTY, 0) < 0) {
    close(sfd);
    close(mfd);
    return -1;
  }

  pg = getpid();
  if(ioctl(sfd, TIOCSPGRP, &pg) < 0) {
    close(sfd);
    close(mfd);
    return -1;
  }
  got = -1;
  if(ioctl(sfd, TIOCGPGRP, &got) < 0) {
    close(sfd);
    close(mfd);
    return -1;
  }

  close(sfd);
  close(mfd);
  if(got != pg)
    return -1;
  return 0;
}

static int
check_pty_bg_signal_isolation(void)
{
  int mfd;
  int sfd;
  int pg;
  int p[2];
  int pid;
  int st;
  char mark;
  struct termios oldt;
  struct termios t;

  mfd = open("/dev/ptmx", O_RDWR);
  if(mfd < 0)
    return -1;
  {
    char sname[32];
    if(ptsname_r(mfd, sname, sizeof(sname)) < 0) {
      close(mfd);
      return -1;
    }
    sfd = open(sname, O_RDWR);
  }
  if(sfd < 0) {
    close(mfd);
    return -1;
  }

  if(ioctl(sfd, TIOCSCTTY, 0) < 0) {
    close(sfd);
    close(mfd);
    return -1;
  }

  pg = getpid();
  if(ioctl(sfd, TIOCSPGRP, &pg) < 0) {
    close(sfd);
    close(mfd);
    return -1;
  }

  if(pipe(p) < 0) {
    close(sfd);
    close(mfd);
    return -1;
  }

  pid = fork();
  if(pid < 0) {
    close(p[0]);
    close(p[1]);
    close(sfd);
    close(mfd);
    return -1;
  }
  if(pid == 0) {
    char ch;
    close(p[0]);
    (void)setpgid(0, 0);
    bg_sig_seen = 0;
    signal(SIGTTIN, termcheck_bg_sig_handler);
    read(sfd, &ch, 1);
    mark = bg_sig_seen ? '1' : '0';
    (void)write(p[1], &mark, 1);
    close(p[1]);
    exit(0);
  }

  close(p[1]);
  mark = '0';
  if(read(p[0], &mark, 1) != 1)
    mark = '0';
  close(p[0]);
  waitpid(pid, &st, 0);
  if(mark != '1') {
    close(sfd);
    close(mfd);
    return -1;
  }

  if(tcgetattr(sfd, &oldt) < 0) {
    close(sfd);
    close(mfd);
    return -1;
  }
  t = oldt;
  t.c_lflag |= TOSTOP;
  if(tcsetattr(sfd, TCSANOW, &t) < 0) {
    close(sfd);
    close(mfd);
    return -1;
  }

  if(pipe(p) < 0) {
    tcsetattr(sfd, TCSANOW, &oldt);
    close(sfd);
    close(mfd);
    return -1;
  }

  pid = fork();
  if(pid < 0) {
    close(p[0]);
    close(p[1]);
    tcsetattr(sfd, TCSANOW, &oldt);
    close(sfd);
    close(mfd);
    return -1;
  }
  if(pid == 0) {
    close(p[0]);
    (void)setpgid(0, 0);
    bg_sig_seen = 0;
    signal(SIGTTOU, termcheck_bg_sig_handler);
    write(sfd, "X", 1);
    mark = bg_sig_seen ? '1' : '0';
    (void)write(p[1], &mark, 1);
    close(p[1]);
    exit(0);
  }

  close(p[1]);
  mark = '0';
  if(read(p[0], &mark, 1) != 1)
    mark = '0';
  close(p[0]);
  waitpid(pid, &st, 0);

  tcsetattr(sfd, TCSANOW, &oldt);
  close(sfd);
  close(mfd);

  if(mark != '1')
    return -1;
  return 0;
}

static int
collect_query_reply(int fd, const char *query, char *buf, int buflen)
{
  int got;
  int n;
  int i;

  if(buflen <= 1)
    return -1;

  (void)ioctl(fd, TCFLSH, TCIFLUSH);
  if(write(fd, query, strlen(query)) != (int)strlen(query))
    return -1;

  got = 0;
  buf[0] = 0;
  for(i = 0; i < 24 && got < buflen - 1; i++) {
    n = read(fd, buf + got, buflen - 1 - got);
    if(n > 0) {
      got += n;
      buf[got] = 0;
    }
  }

  return got;
}

static int
parse_cpr_reply(const char *buf, int n, int dec_private, int *row_out, int *col_out)
{
  int i;
  int found;
  int found_row;
  int found_col;

  found = 0;
  found_row = 0;
  found_col = 0;

  for(i = 0; i + 4 < n; i++) {
    int j;
    int row;
    int col;
    int saw_digit;

    if(buf[i] != '\033' || buf[i + 1] != '[')
      continue;

    j = i + 2;
    if(dec_private) {
      if(j >= n || buf[j] != '?')
        continue;
      j++;
    } else if(j < n && buf[j] == '?') {
      continue;
    }

    row = 0;
    saw_digit = 0;
    while(j < n && buf[j] >= '0' && buf[j] <= '9') {
      row = row * 10 + (buf[j] - '0');
      saw_digit = 1;
      j++;
    }
    if(!saw_digit || j >= n || buf[j] != ';')
      continue;
    j++;

    col = 0;
    saw_digit = 0;
    while(j < n && buf[j] >= '0' && buf[j] <= '9') {
      col = col * 10 + (buf[j] - '0');
      saw_digit = 1;
      j++;
    }
    if(!saw_digit || j >= n || buf[j] != 'R')
      continue;

    found = 1;
    found_row = row;
    found_col = col;
  }

  if(!found)
    return -1;

  if(row_out)
    *row_out = found_row;
  if(col_out)
    *col_out = found_col;
  return 0;
}

static int
parse_window_report(const char *buf, int n, int code, int *a_out, int *b_out)
{
  int i;

  for(i = 0; i + 6 < n; i++) {
    int j;
    int got_code;
    int a;
    int b;
    int saw;

    if(buf[i] != '\033' || buf[i + 1] != '[')
      continue;
    j = i + 2;

    got_code = 0;
    saw = 0;
    while(j < n && buf[j] >= '0' && buf[j] <= '9') {
      got_code = got_code * 10 + (buf[j] - '0');
      saw = 1;
      j++;
    }
    if(!saw || got_code != code || j >= n || buf[j] != ';')
      continue;
    j++;

    a = 0;
    saw = 0;
    while(j < n && buf[j] >= '0' && buf[j] <= '9') {
      a = a * 10 + (buf[j] - '0');
      saw = 1;
      j++;
    }
    if(!saw)
      continue;

    b = -1;
    if(j < n && buf[j] == ';') {
      j++;
      b = 0;
      saw = 0;
      while(j < n && buf[j] >= '0' && buf[j] <= '9') {
        b = b * 10 + (buf[j] - '0');
        saw = 1;
        j++;
      }
      if(!saw)
        continue;
    }

    if(j >= n || buf[j] != 't')
      continue;

    if(a_out)
      *a_out = a;
    if(b_out)
      *b_out = b;
    return 0;
  }

  return -1;
}

static int
check_terminal_query_replies(int fd)
{
  struct termios oldt;
  struct termios t;
  struct winsize ws;
  char buf[128];
  int row;
  int col;
  int exp_row;
  int exp_col;

  if(tcgetattr(fd, &oldt) < 0)
    return -1;

  t = oldt;
  t.c_lflag &= ~(ICANON | ECHO);
  t.c_cc[VMIN] = 0;
  t.c_cc[VTIME] = 1;
  if(tcsetattr(fd, TCSANOW, &t) < 0)
    return -1;

  exp_row = 24;
  exp_col = 80;
  if(ioctl(fd, TIOCGWINSZ, &ws) == 0) {
    if(ws.ws_row > 0)
      exp_row = ws.ws_row;
    if(ws.ws_col > 0)
      exp_col = ws.ws_col;
  }

  if(expect_token_reply(fd, "DSR 5n", "\033[5n", "\033[0n", buf, sizeof(buf)) < 0) {
    tcsetattr(fd, TCSANOW, &oldt);
    return -1;
  }

  if(collect_query_reply(fd, "\033[6n", buf, sizeof(buf)) < 0 || parse_cpr_reply(buf, strlen(buf), 0, &row, &col) < 0) {
    tc_debug_query_fail("DSR 6n", "\033[6n", "CPR", buf, strlen(buf));
    tcsetattr(fd, TCSANOW, &oldt);
    return -1;
  }

  if(collect_query_reply(fd, "\033[?6n", buf, sizeof(buf)) < 0 || parse_cpr_reply(buf, strlen(buf), 1, &row, &col) < 0) {
    tc_debug_query_fail("DEC-CPR ?6n", "\033[?6n", "DEC CPR", buf, strlen(buf));
    tcsetattr(fd, TCSANOW, &oldt);
    return -1;
  }

  if(expect_token_reply(fd, "DEC DSR ?15n", "\033[?15n", "\033[?13n", buf, sizeof(buf)) < 0) {
    tcsetattr(fd, TCSANOW, &oldt);
    return -1;
  }

  if(expect_token_reply(fd, "DEC DSR ?25n", "\033[?25n", "\033[?21n", buf, sizeof(buf)) < 0) {
    tcsetattr(fd, TCSANOW, &oldt);
    return -1;
  }

  if(expect_token_reply(fd, "DA ESC Z", "\033Z", "\033[?1;0c", buf, sizeof(buf)) < 0) {
    tcsetattr(fd, TCSANOW, &oldt);
    return -1;
  }

  if(expect_token_reply(fd, "DA primary", "\033[c", "\033[?1;0c", buf, sizeof(buf)) < 0) {
    tcsetattr(fd, TCSANOW, &oldt);
    return -1;
  }

  if(expect_token_reply(fd, "DA secondary", "\033[>c", "\033[>0;0;0c", buf, sizeof(buf)) < 0) {
    tcsetattr(fd, TCSANOW, &oldt);
    return -1;
  }

  if(expect_token_reply(fd, "XTWINOPS 11t", "\033[11t", "\033[1t", buf, sizeof(buf)) < 0) {
    tcsetattr(fd, TCSANOW, &oldt);
    return -1;
  }

  if(expect_token_reply(fd, "XTWINOPS 13t", "\033[13t", "\033[3;1;1t", buf, sizeof(buf)) < 0) {
    tcsetattr(fd, TCSANOW, &oldt);
    return -1;
  }

  if(collect_query_reply(fd, "\033[14t", buf, sizeof(buf)) < 0 ||
     parse_window_report(buf, strlen(buf), 4, &row, &col) < 0 ||
     row != exp_row * 16 || col != exp_col * 8) {
    tc_debug_query_fail("XTWINOPS 14t", "\033[14t", "CSI 4;rows*16;cols*8 t", buf, strlen(buf));
    tcsetattr(fd, TCSANOW, &oldt);
    return -1;
  }

  if(collect_query_reply(fd, "\033[15t", buf, sizeof(buf)) < 0 ||
     parse_window_report(buf, strlen(buf), 5, &row, &col) < 0 ||
     row != exp_row * 16 || col != exp_col * 8) {
    tc_debug_query_fail("XTWINOPS 15t", "\033[15t", "CSI 5;rows*16;cols*8 t", buf, strlen(buf));
    tcsetattr(fd, TCSANOW, &oldt);
    return -1;
  }

  if(collect_query_reply(fd, "\033[16t", buf, sizeof(buf)) < 0 ||
     parse_window_report(buf, strlen(buf), 6, &row, &col) < 0 ||
     row != 16 || col != 8) {
    tc_debug_query_fail("XTWINOPS 16t", "\033[16t", "CSI 6;16;8 t", buf, strlen(buf));
    tcsetattr(fd, TCSANOW, &oldt);
    return -1;
  }

  if(collect_query_reply(fd, "\033[18t", buf, sizeof(buf)) < 0 ||
     parse_window_report(buf, strlen(buf), 8, &row, &col) < 0 ||
     row != exp_row || col != exp_col) {
    tc_debug_query_fail("XTWINOPS 18t", "\033[18t", "CSI 8;rows;cols t", buf, strlen(buf));
    tcsetattr(fd, TCSANOW, &oldt);
    return -1;
  }

  if(collect_query_reply(fd, "\033[19t", buf, sizeof(buf)) < 0 ||
     parse_window_report(buf, strlen(buf), 9, &row, &col) < 0 ||
     row != exp_row || col != exp_col) {
    tc_debug_query_fail("XTWINOPS 19t", "\033[19t", "CSI 9;rows;cols t", buf, strlen(buf));
    tcsetattr(fd, TCSANOW, &oldt);
    return -1;
  }

  if(expect_token_reply(fd, "XTWINOPS 20t", "\033[20t", "auxv6", buf, sizeof(buf)) < 0) {
    tcsetattr(fd, TCSANOW, &oldt);
    return -1;
  }

  if(expect_token_reply(fd, "XTWINOPS 21t", "\033[21t", "auxv6", buf, sizeof(buf)) < 0) {
    tcsetattr(fd, TCSANOW, &oldt);
    return -1;
  }

  drain_tty_input_nonblock(fd);
  (void)ioctl(fd, TCFLSH, TCIFLUSH);

  tcsetattr(fd, TCSANOW, &oldt);
  return 0;
}

static int
check_terminal_parser_sequences(int fd)
{
  struct termios oldt;
  struct termios t;
  char buf[128];
  int n;
  int row;
  int col;

  if(tcgetattr(fd, &oldt) < 0)
    return -1;

  t = oldt;
  t.c_lflag &= ~(ICANON | ECHO);
  t.c_cc[VMIN] = 0;
  t.c_cc[VTIME] = 1;
  if(tcsetattr(fd, TCSANOW, &t) < 0)
    return -1;

  n = collect_query_reply(fd, "\033[3;10H\033[6n", buf, sizeof(buf));
  if(n < 0 || parse_cpr_reply(buf, n, 0, &row, &col) < 0 || row != 3 || col != 10) {
    tcsetattr(fd, TCSANOW, &oldt);
    return -1;
  }

  n = collect_query_reply(fd, "\033[4;10H\033[3X\033[6n", buf, sizeof(buf));
  if(n < 0 || parse_cpr_reply(buf, n, 0, &row, &col) < 0 || row != 4 || col != 10) {
    tcsetattr(fd, TCSANOW, &oldt);
    return -1;
  }

  n = collect_query_reply(fd, "\033[5;10HZ\033[5b\033[6n", buf, sizeof(buf));
  if(n < 0 || parse_cpr_reply(buf, n, 0, &row, &col) < 0 || row != 5 || col != 16) {
    tcsetattr(fd, TCSANOW, &oldt);
    return -1;
  }

  n = collect_query_reply(fd, "\033[6;10H\033[4hQ\033[4l\033[6n", buf, sizeof(buf));
  if(n < 0 || parse_cpr_reply(buf, n, 0, &row, &col) < 0 || row != 6 || col != 11) {
    tcsetattr(fd, TCSANOW, &oldt);
    return -1;
  }

  n = collect_query_reply(fd, "\033]0;auxv6\a\033[7;10H\033[6n", buf, sizeof(buf));
  if(n < 0 || parse_cpr_reply(buf, n, 0, &row, &col) < 0 || row != 7 || col != 10) {
    tc_debug_query_fail("OSC BEL terminator", "\\033]0;auxv6\\a", "CPR after OSC", buf, n);
    tcsetattr(fd, TCSANOW, &oldt);
    return -1;
  }

  n = collect_query_reply(fd, "\033]0;auxv6\033\\\033[8;10H\033[6n", buf, sizeof(buf));
  if(n < 0 || parse_cpr_reply(buf, n, 0, &row, &col) < 0 || row != 8 || col != 10) {
    tc_debug_query_fail("OSC ST terminator", "\\033]0;auxv6\\033\\\\", "CPR after OSC", buf, n);
    tcsetattr(fd, TCSANOW, &oldt);
    return -1;
  }

  drain_tty_input_nonblock(fd);
  (void)ioctl(fd, TCFLSH, TCIFLUSH);
  (void)write(fd, "\033[?7h\033[4l", 8);
  tcsetattr(fd, TCSANOW, &oldt);
  return 0;
}

static int
check_terminal_mode_query_replies(int fd)
{
  struct termios oldt;
  struct termios t;
  char buf[128];

  if(tcgetattr(fd, &oldt) < 0)
    return -1;

  t = oldt;
  t.c_lflag &= ~(ICANON | ECHO);
  t.c_cc[VMIN] = 0;
  t.c_cc[VTIME] = 1;
  if(tcsetattr(fd, TCSANOW, &t) < 0)
    return -1;

  if(expect_token_reply(fd, "RMQ 4 set", "\033[4h\033[4$p", "\033[4;1$y", buf, sizeof(buf)) < 0 ||
     expect_token_reply(fd, "RMQ 4 reset", "\033[4l\033[4$p", "\033[4;2$y", buf, sizeof(buf)) < 0 ||
     expect_token_reply(fd, "DECRQM ?25 reset", "\033[?25l\033[?25$p", "\033[?25;2$y", buf, sizeof(buf)) < 0 ||
     expect_token_reply(fd, "DECRQM ?25 set", "\033[?25h\033[?25$p", "\033[?25;1$y", buf, sizeof(buf)) < 0 ||
     expect_token_reply(fd, "DECRQM ?7 set", "\033[?7h\033[?7$p", "\033[?7;1$y", buf, sizeof(buf)) < 0 ||
     expect_token_reply(fd, "DECRQM ?7 reset", "\033[?7l\033[?7$p", "\033[?7;2$y", buf, sizeof(buf)) < 0 ||
     expect_token_reply(fd, "DECRQM ?6 set", "\033[?6h\033[?6$p", "\033[?6;1$y", buf, sizeof(buf)) < 0 ||
     expect_token_reply(fd, "DECRQM ?6 reset", "\033[?6l\033[?6$p", "\033[?6;2$y", buf, sizeof(buf)) < 0 ||
     expect_token_reply(fd, "DECRQM ?1 set", "\033[?1h\033[?1$p", "\033[?1;1$y", buf, sizeof(buf)) < 0 ||
     expect_token_reply(fd, "DECRQM ?1 reset", "\033[?1l\033[?1$p", "\033[?1;2$y", buf, sizeof(buf)) < 0 ||
     expect_token_reply(fd, "DECRQM ?5 set", "\033[?5h\033[?5$p", "\033[?5;1$y", buf, sizeof(buf)) < 0 ||
     expect_token_reply(fd, "DECRQM ?5 reset", "\033[?5l\033[?5$p", "\033[?5;2$y", buf, sizeof(buf)) < 0 ||
     expect_token_reply(fd, "DECRQM ?12 reset", "\033[?12l\033[?12$p", "\033[?12;2$y", buf, sizeof(buf)) < 0 ||
     expect_token_reply(fd, "DECRQM ?12 set", "\033[?12h\033[?12$p", "\033[?12;1$y", buf, sizeof(buf)) < 0 ||
     expect_token_reply(fd, "DECRQM ?1049 set", "\033[?1049h\033[?1049$p", "\033[?1049;1$y", buf, sizeof(buf)) < 0 ||
     expect_token_reply(fd, "DECRQM ?1049 reset", "\033[?1049l\033[?1049$p", "\033[?1049;2$y", buf, sizeof(buf)) < 0 ||
     expect_token_reply(fd, "RMQ 20 set", "\033[20h\033[20$p", "\033[20;1$y", buf, sizeof(buf)) < 0 ||
     expect_token_reply(fd, "RMQ 20 reset", "\033[20l\033[20$p", "\033[20;2$y", buf, sizeof(buf)) < 0 ||
     expect_token_reply(fd, "DECRQM ?1048 set", "\033[?1048h\033[?1048$p", "\033[?1048;1$y", buf, sizeof(buf)) < 0 ||
     expect_token_reply(fd, "DECRQM ?1048 reset", "\033[?1048l\033[?1048$p", "\033[?1048;2$y", buf, sizeof(buf)) < 0 ||
     expect_token_reply(fd, "DECRQM ?1000 set", "\033[?1000h\033[?1000$p", "\033[?1000;1$y", buf, sizeof(buf)) < 0 ||
     expect_token_reply(fd, "DECRQM ?1000 reset", "\033[?1000l\033[?1000$p", "\033[?1000;2$y", buf, sizeof(buf)) < 0 ||
     expect_token_reply(fd, "DECRQM ?1002 set", "\033[?1002h\033[?1002$p", "\033[?1002;1$y", buf, sizeof(buf)) < 0 ||
     expect_token_reply(fd, "DECRQM ?1002 reset", "\033[?1002l\033[?1002$p", "\033[?1002;2$y", buf, sizeof(buf)) < 0 ||
     expect_token_reply(fd, "DECRQM ?1003 set", "\033[?1003h\033[?1003$p", "\033[?1003;1$y", buf, sizeof(buf)) < 0 ||
     expect_token_reply(fd, "DECRQM ?1003 reset", "\033[?1003l\033[?1003$p", "\033[?1003;2$y", buf, sizeof(buf)) < 0 ||
     expect_token_reply(fd, "DECRQM ?1004 set", "\033[?1004h\033[?1004$p", "\033[?1004;1$y", buf, sizeof(buf)) < 0 ||
     expect_token_reply(fd, "DECRQM ?1004 reset", "\033[?1004l\033[?1004$p", "\033[?1004;2$y", buf, sizeof(buf)) < 0 ||
     expect_token_reply(fd, "DECRQM ?1005 set", "\033[?1005h\033[?1005$p", "\033[?1005;1$y", buf, sizeof(buf)) < 0 ||
     expect_token_reply(fd, "DECRQM ?1005 reset", "\033[?1005l\033[?1005$p", "\033[?1005;2$y", buf, sizeof(buf)) < 0 ||
     expect_token_reply(fd, "DECRQM ?1006 set", "\033[?1006h\033[?1006$p", "\033[?1006;1$y", buf, sizeof(buf)) < 0 ||
     expect_token_reply(fd, "DECRQM ?1006 reset", "\033[?1006l\033[?1006$p", "\033[?1006;2$y", buf, sizeof(buf)) < 0 ||
     expect_token_reply(fd, "DECRQM ?1015 set", "\033[?1015h\033[?1015$p", "\033[?1015;1$y", buf, sizeof(buf)) < 0 ||
     expect_token_reply(fd, "DECRQM ?1015 reset", "\033[?1015l\033[?1015$p", "\033[?1015;2$y", buf, sizeof(buf)) < 0 ||
     expect_token_reply(fd, "DECRQM ?1007 set", "\033[?1007h\033[?1007$p", "\033[?1007;1$y", buf, sizeof(buf)) < 0 ||
     expect_token_reply(fd, "DECRQM ?1007 reset", "\033[?1007l\033[?1007$p", "\033[?1007;2$y", buf, sizeof(buf)) < 0 ||
     expect_token_reply(fd, "DECRQM ?1034 set", "\033[?1034h\033[?1034$p", "\033[?1034;1$y", buf, sizeof(buf)) < 0 ||
     expect_token_reply(fd, "DECRQM ?1034 reset", "\033[?1034l\033[?1034$p", "\033[?1034;2$y", buf, sizeof(buf)) < 0 ||
     expect_token_reply(fd, "DECRQM ?1035 set", "\033[?1035h\033[?1035$p", "\033[?1035;1$y", buf, sizeof(buf)) < 0 ||
     expect_token_reply(fd, "DECRQM ?1035 reset", "\033[?1035l\033[?1035$p", "\033[?1035;2$y", buf, sizeof(buf)) < 0 ||
     expect_token_reply(fd, "DECRQM ?1036 set", "\033[?1036h\033[?1036$p", "\033[?1036;1$y", buf, sizeof(buf)) < 0 ||
     expect_token_reply(fd, "DECRQM ?1036 reset", "\033[?1036l\033[?1036$p", "\033[?1036;2$y", buf, sizeof(buf)) < 0 ||
     expect_token_reply(fd, "DECRQM ?1039 set", "\033[?1039h\033[?1039$p", "\033[?1039;1$y", buf, sizeof(buf)) < 0 ||
     expect_token_reply(fd, "DECRQM ?1039 reset", "\033[?1039l\033[?1039$p", "\033[?1039;2$y", buf, sizeof(buf)) < 0 ||
     expect_token_reply(fd, "DECRQM ?2004 set", "\033[?2004h\033[?2004$p", "\033[?2004;1$y", buf, sizeof(buf)) < 0 ||
     expect_token_reply(fd, "DECRQM ?2004 reset", "\033[?2004l\033[?2004$p", "\033[?2004;2$y", buf, sizeof(buf)) < 0 ||
     expect_token_reply(fd, "DECRQM ?9 set", "\033[?9h\033[?9$p", "\033[?9;1$y", buf, sizeof(buf)) < 0 ||
     expect_token_reply(fd, "DECRQM ?9 reset", "\033[?9l\033[?9$p", "\033[?9;2$y", buf, sizeof(buf)) < 0 ||
     expect_token_reply(fd, "DECRQM ?67 set", "\033[?67h\033[?67$p", "\033[?67;1$y", buf, sizeof(buf)) < 0 ||
     expect_token_reply(fd, "DECRQM ?67 reset", "\033[?67l\033[?67$p", "\033[?67;2$y", buf, sizeof(buf)) < 0 ||
    expect_token_reply(fd, "DECRQM ?66 set", "\033=\033[?66$p", "\033[?66;1$y", buf, sizeof(buf)) < 0 ||
    expect_token_reply(fd, "DECRQM ?66 reset", "\033>\033[?66$p", "\033[?66;2$y", buf, sizeof(buf)) < 0 ||
     expect_token_reply(fd, "DECRQM ?69 set", "\033[?69h\033[?69$p", "\033[?69;1$y", buf, sizeof(buf)) < 0 ||
     expect_token_reply(fd, "DECRQM ?69 reset", "\033[?69l\033[?69$p", "\033[?69;2$y", buf, sizeof(buf)) < 0 ||
     expect_token_reply(fd, "DECRQM ?95 set", "\033[?95h\033[?95$p", "\033[?95;1$y", buf, sizeof(buf)) < 0 ||
     expect_token_reply(fd, "DECRQM ?95 reset", "\033[?95l\033[?95$p", "\033[?95;2$y", buf, sizeof(buf)) < 0 ||
     expect_token_reply(fd, "DECRQM ?1001 set", "\033[?1001h\033[?1001$p", "\033[?1001;1$y", buf, sizeof(buf)) < 0 ||
     expect_token_reply(fd, "DECRQM ?1001 reset", "\033[?1001l\033[?1001$p", "\033[?1001;2$y", buf, sizeof(buf)) < 0 ||
     expect_token_reply(fd, "RMQ unknown 999", "\033[999$p", "\033[999;0$y", buf, sizeof(buf)) < 0 ||
     expect_token_reply(fd, "DECRQM unknown ?999", "\033[?999$p", "\033[?999;0$y", buf, sizeof(buf)) < 0) {
    tcsetattr(fd, TCSANOW, &oldt);
    return -1;
  }

  drain_tty_input_nonblock(fd);
  (void)ioctl(fd, TCFLSH, TCIFLUSH);

  tcsetattr(fd, TCSANOW, &oldt);
  return 0;
}

static int
check_terminal_alt_screen_invariants(int fd)
{
  struct termios oldt;
  struct termios t;
  char buf[128];
  int n;
  int row;
  int col;

  if(tcgetattr(fd, &oldt) < 0)
    return -1;

  t = oldt;
  t.c_lflag &= ~(ICANON | ECHO);
  t.c_cc[VMIN] = 0;
  t.c_cc[VTIME] = 1;
  if(tcsetattr(fd, TCSANOW, &t) < 0)
    return -1;

  n = collect_query_reply(fd, "\033[10;20H\033[6n", buf, sizeof(buf));
  if(n < 0 || parse_cpr_reply(buf, n, 0, &row, &col) < 0 || row != 10 || col != 20) {
    tcsetattr(fd, TCSANOW, &oldt);
    return -1;
  }

  n = collect_query_reply(fd, "\033[?1049h\033[6n", buf, sizeof(buf));
  if(n < 0 || parse_cpr_reply(buf, n, 0, &row, &col) < 0 ||
     !((row == 1 && col == 1) || (row == 10 && col == 20))) {
    tcsetattr(fd, TCSANOW, &oldt);
    return -1;
  }

  n = collect_query_reply(fd, "\033[3;5H\033[?1049l\033[6n", buf, sizeof(buf));
  if(n < 0 || parse_cpr_reply(buf, n, 0, &row, &col) < 0 || row != 10 || col != 20) {
    tcsetattr(fd, TCSANOW, &oldt);
    return -1;
  }

  drain_tty_input_nonblock(fd);
  (void)ioctl(fd, TCFLSH, TCIFLUSH);
  tcsetattr(fd, TCSANOW, &oldt);
  return 0;
}

static int
check_terminal_origin_scroll_invariants(int fd)
{
  struct termios oldt;
  struct termios t;
  char buf[128];
  int n;
  int row;
  int col;

  if(tcgetattr(fd, &oldt) < 0)
    return -1;

  t = oldt;
  t.c_lflag &= ~(ICANON | ECHO);
  t.c_cc[VMIN] = 0;
  t.c_cc[VTIME] = 1;
  if(tcsetattr(fd, TCSANOW, &t) < 0)
    return -1;

  n = collect_query_reply(fd, "\033[r\033[5;10r\033[?6h\033[1;1H\033[6n", buf, sizeof(buf));
  if(n < 0 || parse_cpr_reply(buf, n, 0, &row, &col) < 0 || row != 5 || col != 1) {
    tcsetattr(fd, TCSANOW, &oldt);
    return -1;
  }

  n = collect_query_reply(fd, "\033[?6l\033[1;1H\033[6n", buf, sizeof(buf));
  if(n < 0 || parse_cpr_reply(buf, n, 0, &row, &col) < 0 || row != 1 || col != 1) {
    tcsetattr(fd, TCSANOW, &oldt);
    return -1;
  }

  drain_tty_input_nonblock(fd);
  (void)ioctl(fd, TCFLSH, TCIFLUSH);
  (void)write(fd, "\033[r\033[?6l", 8);
  tcsetattr(fd, TCSANOW, &oldt);
  return 0;
}

int
main(int argc, char **argv)
{
  int fd;
  int fails;
  int smoke_mode;
  int debug_query;
  const char *log_path;
  int i;

  smoke_mode = 0;
  debug_query = 0;
  log_path = "/tmp/termcheck.log";
  for(i = 1; i < argc; i++) {
    if(strcmp(argv[i], "--smoke") == 0) {
      smoke_mode = 1;
    } else if(strcmp(argv[i], "--debug-query") == 0) {
      debug_query = 1;
    } else if(strcmp(argv[i], "--log") == 0 && i + 1 < argc) {
      log_path = argv[++i];
    }
  }

  tc_debug_query = debug_query;

  tc_log_fd = open(log_path, O_CREATE | O_TRUNC | O_WRONLY);
  if(tc_log_fd >= 0)
    tc_report(1, "termcheck: status log enabled");
  if(tc_debug_query)
    tc_report(1, "termcheck: query debug enabled");

  fd = 0;
  if(!isatty(fd))
    fd = 1;
  if(!isatty(fd))
    fd = 2;
  if(!isatty(fd)) {
    tc_report(2, "termcheck: no tty fd available");
    if(tc_log_fd >= 0)
      close(tc_log_fd);
    exit(0);
  }

  fails = 0;

  if(check_termios_roundtrip(fd) < 0) {
    tc_report(2, "FAIL: termios roundtrip");
    fails++;
  } else {
    tc_report(1, "PASS: termios roundtrip");
  }

  if(!smoke_mode) {
    if(check_noncanon_vmin0_vtime0(fd) < 0) {
      tc_report(2, "FAIL: noncanon VMIN=0 VTIME=0 immediate read");
      fails++;
    } else {
      tc_report(1, "PASS: noncanon VMIN=0 VTIME=0 immediate read");
    }
  } else {
    tc_report(1, "SKIP: noncanon VMIN=0 VTIME=0 immediate read (smoke)");
  }

  if(check_winsize_ioctl(fd) < 0) {
    tc_report(2, "FAIL: winsize ioctl");
    fails++;
  } else {
    tc_report(1, "PASS: winsize ioctl");
  }

  if(check_tcsetattr_optional_actions(fd) < 0) {
    tc_report(2, "FAIL: tcsetattr optional actions");
    fails++;
  } else {
    tc_report(1, "PASS: tcsetattr optional actions");
  }

  if(check_flag_roundtrip(fd) < 0) {
    tc_report(2, "FAIL: ISTRIP/ECHOCTL flag roundtrip");
    fails++;
  } else {
    tc_report(1, "PASS: ISTRIP/ECHOCTL flag roundtrip");
  }

  if(check_tty_identity_helpers(fd) < 0) {
    tc_report(2, "FAIL: ttyname/isatty identity helpers");
    fails++;
  } else {
    tc_report(1, "PASS: ttyname/isatty identity helpers");
  }

  if(check_pty_pgrp_ioctl() < 0) {
    tc_report(2, "FAIL: pty TIOCSCTTY/TIOCSPGRP/TIOCGPGRP");
    fails++;
  } else {
    tc_report(1, "PASS: pty TIOCSCTTY/TIOCSPGRP/TIOCGPGRP");
  }

  if(check_pty_bg_signal_isolation() < 0) {
    tc_report(2, "FAIL: pty background SIGTTIN/SIGTTOU isolation");
    fails++;
  } else {
    tc_report(1, "PASS: pty background SIGTTIN/SIGTTOU isolation");
  }

  if(!smoke_mode) {
    if(check_terminal_query_replies(fd) < 0) {
      tc_report(2, "FAIL: terminal DSR/DA query replies");
      fails++;
    } else {
      tc_report(1, "PASS: terminal DSR/DA query replies");
    }

    if(check_terminal_parser_sequences(fd) < 0) {
      tc_report(2, "FAIL: terminal parser cursor/wrap/REP probes");
      fails++;
    } else {
      tc_report(1, "PASS: terminal parser cursor/wrap/REP probes");
    }

    if(check_terminal_mode_query_replies(fd) < 0) {
      tc_report(2, "FAIL: terminal mode query replies (RMQ/DECRQM)");
      fails++;
    } else {
      tc_report(1, "PASS: terminal mode query replies (RMQ/DECRQM)");
    }

    if(check_terminal_origin_scroll_invariants(fd) < 0) {
      tc_report(2, "FAIL: terminal origin/scroll-region invariants");
      fails++;
    } else {
      tc_report(1, "PASS: terminal origin/scroll-region invariants");
    }

    if(check_terminal_alt_screen_invariants(fd) < 0) {
      tc_report(2, "FAIL: terminal alt-screen cursor invariants");
      fails++;
    } else {
      tc_report(1, "PASS: terminal alt-screen cursor invariants");
    }
  } else {
    tc_report(1, "SKIP: terminal DSR/DA query replies (smoke)");
    tc_report(1, "SKIP: terminal parser cursor/wrap/REP probes (smoke)");
    tc_report(1, "SKIP: terminal mode query replies (RMQ/DECRQM) (smoke)");
    tc_report(1, "SKIP: terminal origin/scroll-region invariants (smoke)");
    tc_report(1, "SKIP: terminal alt-screen cursor invariants (smoke)");
  }

  if(check_ioctl_termios_compat(fd) < 0) {
    tc_report(2, "FAIL: ioctl TCGETS/TCSETS* compatibility");
    fails++;
  } else {
    tc_report(1, "PASS: ioctl TCGETS/TCSETS* compatibility");
  }

  if(check_ioctl_queue_state(fd) < 0) {
    tc_report(2, "FAIL: ioctl queue state (FIONREAD/TIOCOUTQ)");
    fails++;
  } else {
    tc_report(1, "PASS: ioctl queue state (FIONREAD/TIOCOUTQ)");
  }

  if(check_pty_roundtrip() < 0) {
    tc_report(2, "FAIL: pty /dev/ptmx <-> /dev/pts/N roundtrip");
    fails++;
  } else {
    tc_report(1, "PASS: pty /dev/ptmx <-> /dev/pts/N roundtrip");
  }

  if(!smoke_mode) {
    if(check_multi_pty_shells() < 0) {
      tc_report(2, "FAIL: multi-pty shell isolation/lifecycle");
      fails++;
    } else {
      tc_report(1, "PASS: multi-pty shell isolation/lifecycle");
    }

    if(check_pty_max_stress() < 0) {
      tc_report(2, "FAIL: pty max create/terminate stress");
      fails++;
    } else {
      tc_report(1, "PASS: pty max create/terminate stress");
    }
  } else {
    tc_report(1, "SKIP: multi-pty shell isolation/lifecycle (smoke)");
    tc_report(1, "SKIP: pty max create/terminate stress (smoke)");
  }

  if(fails == 0)
    tc_report(1, "termcheck: all checks passed");
  else
    tc_report_failcount(fails);

  if(tc_log_fd >= 0)
    close(tc_log_fd);

  exit(0);
  return 0;
}
