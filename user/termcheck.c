#include "../include/types.h"
#include "../include/user.h"
#include "../include/fcntl.h"
#include "../include/termios.h"
#include "../include/posix/sys/ioctl.h"

int ptsname_r(int fd, char *buf, uint buflen);

#define PTY_SHELL_SESSIONS 3
#define PTY_POLL_LOOPS 80
#define PTY_STRESS_CAP 64
#define PTY_STRESS_CYCLES 8

struct pty_shell_session {
  int mfd;
  int pid;
  int seen;
  char marker[16];
};

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
      exit();

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
    printf(1, "termcheck: spawned shell on PTY %d\n", i);
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
          printf(1, "termcheck: shell PTY %d produced marker %s\n", i, sess[i].marker);
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

    sleep(2);
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

  printf(1, "termcheck: all PTY shells terminated\n");
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
    if(ptsname_r(mfd, sname, sizeof(sname)) < 0) {
      close(mfd);
      break;
    }
    sfd = open(sname, O_RDWR);
    if(sfd < 0) {
      close(mfd);
      break;
    }
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
  int count;
  int expected;
  int i;
  int extra;

  expected = -1;
  for(i = 0; i < PTY_STRESS_CYCLES; i++) {
    count = open_pty_batch(mfds, sfds, PTY_STRESS_CAP);
    if(count <= 0) {
      close_pty_batch(mfds, sfds, count);
      return -1;
    }

    if(expected < 0)
      expected = count;
    if(count != expected) {
      close_pty_batch(mfds, sfds, count);
      return -1;
    }

    extra = open("/dev/ptmx", O_RDWR);
    if(extra >= 0) {
      close(extra);
      close_pty_batch(mfds, sfds, count);
      return -1;
    }

    close_pty_batch(mfds, sfds, count);
    printf(1, "termcheck: pty stress cycle %d/%d max=%d\n", i + 1, PTY_STRESS_CYCLES, count);
  }

  if(expected < PTY_SHELL_SESSIONS)
    return -1;
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
  char ch;
  int n;

  if(tcgetattr(fd, &oldt) < 0)
    return -1;

  t = oldt;
  t.c_lflag &= ~(ICANON | ECHO);
  t.c_cc[VMIN] = 0;
  t.c_cc[VTIME] = 0;

  if(tcsetattr(fd, TCSANOW, &t) < 0)
    return -1;

  n = read(fd, &ch, 1);
  tcsetattr(fd, TCSANOW, &oldt);

  if(n != 0)
    return -1;
  return 0;
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
  testw.ws_row = 24;
  testw.ws_col = 80;

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

int
main(int argc, char **argv)
{
  int fd;
  int fails;

  (void)argc;
  (void)argv;

  fd = 0;
  if(!isatty(fd))
    fd = 1;
  if(!isatty(fd))
    fd = 2;
  if(!isatty(fd)) {
    printf(2, "termcheck: no tty fd available\n");
    exit();
  }

  fails = 0;

  if(check_termios_roundtrip(fd) < 0) {
    printf(2, "FAIL: termios roundtrip\n");
    fails++;
  } else {
    printf(1, "PASS: termios roundtrip\n");
  }

  if(check_noncanon_vmin0_vtime0(fd) < 0) {
    printf(2, "FAIL: noncanon VMIN=0 VTIME=0 immediate read\n");
    fails++;
  } else {
    printf(1, "PASS: noncanon VMIN=0 VTIME=0 immediate read\n");
  }

  if(check_winsize_ioctl(fd) < 0) {
    printf(2, "FAIL: winsize ioctl\n");
    fails++;
  } else {
    printf(1, "PASS: winsize ioctl\n");
  }

  if(check_tcsetattr_optional_actions(fd) < 0) {
    printf(2, "FAIL: tcsetattr optional actions\n");
    fails++;
  } else {
    printf(1, "PASS: tcsetattr optional actions\n");
  }

  if(check_flag_roundtrip(fd) < 0) {
    printf(2, "FAIL: ISTRIP/ECHOCTL flag roundtrip\n");
    fails++;
  } else {
    printf(1, "PASS: ISTRIP/ECHOCTL flag roundtrip\n");
  }

  if(check_ioctl_termios_compat(fd) < 0) {
    printf(2, "FAIL: ioctl TCGETS/TCSETS* compatibility\n");
    fails++;
  } else {
    printf(1, "PASS: ioctl TCGETS/TCSETS* compatibility\n");
  }

  if(check_ioctl_queue_state(fd) < 0) {
    printf(2, "FAIL: ioctl queue state (FIONREAD/TIOCOUTQ)\n");
    fails++;
  } else {
    printf(1, "PASS: ioctl queue state (FIONREAD/TIOCOUTQ)\n");
  }

  if(check_pty_roundtrip() < 0) {
    printf(2, "FAIL: pty /dev/ptmx <-> /dev/pts/N roundtrip\n");
    fails++;
  } else {
    printf(1, "PASS: pty /dev/ptmx <-> /dev/pts/N roundtrip\n");
  }

  if(check_multi_pty_shells() < 0) {
    printf(2, "FAIL: multi-pty shell isolation/lifecycle\n");
    fails++;
  } else {
    printf(1, "PASS: multi-pty shell isolation/lifecycle\n");
  }

  if(check_pty_max_stress() < 0) {
    printf(2, "FAIL: pty max create/terminate stress\n");
    fails++;
  } else {
    printf(1, "PASS: pty max create/terminate stress\n");
  }

  if(fails == 0)
    printf(1, "termcheck: all checks passed\n");
  else
    printf(2, "termcheck: %d checks failed\n", fails);

  exit();
  return 0;
}
