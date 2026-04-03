#include "types.h"
#include "auxv6/user.h"
#include "socket.h"
#include "termios.h"
#include "signal.h"
#include "sys/ioctl.h"

#define TELNET_DEFAULT_PORT 23
#define TELNET_BUF 512

#define IAC  255
#define DONT 254
#define DO   253
#define WONT 252
#define WILL 251
#define SB   250
#define SE   240

/* Telnet options */
#define OPT_NAWS 31   /* Negotiate About Window Size (RFC 1073) */

#define TELNET_ESCAPE 29  /* Ctrl-] */
#define TELNET_CTRL_C 3
#define TELNET_CTRL_Z 26
#define TELNET_RECV_POLL_TICKS 10

/* NAWS state */
static int naws_enabled = 0;    /* server has agreed to receive our window size */
static volatile int naws_dirty = 0; /* SIGWINCH fired; need to resend NAWS */

static void
usage(void)
{
  dprintf(2, "usage: telnet host [port]\n");
  exit(0);
}

static int
parse_port(const char *s)
{
  int p;

  if(s == 0)
    return -1;
  p = atoi(s);
  if(p < 1 || p > 65535)
    return -1;
  return p;
}

static int
open_telnet(const char *host, int port)
{
  int fd;
  uint ip;
  struct sockaddr_in dst;

  if(resolve_ipv4(host, &ip) < 0) {
    dprintf(2, "telnet: cannot resolve host %s\n", host);
    return -1;
  }

  fd = socket(AF_INET, SOCK_STREAM, 0);
  if(fd < 0) {
    dprintf(2, "telnet: socket failed\n");
    return -1;
  }

  memset(&dst, 0, sizeof(dst));
  dst.sin_family = AF_INET;
  dst.sin_port = (ushort)port;
  dst.sin_addr = ip;

  if(connect(fd, &dst, sizeof(dst)) < 0) {
    dprintf(2, "telnet: connect failed\n");
    close(fd);
    return -1;
  }

  return fd;
}

static void
sigwinch_handler(int sig)
{
  (void)sig;
  naws_dirty = 1;
}

static void
send_iac_reply(int fd, uchar cmd, uchar opt)
{
  uchar rep[3];

  rep[0] = IAC;
  rep[1] = cmd;
  rep[2] = opt;
  send(fd, rep, 3);
}

/*
 * Send Negotiate About Window Size subnegotiation (RFC 1073).
 * Reads the current terminal window size via TIOCGWINSZ and sends:
 *   IAC SB NAWS <width-high> <width-low> <height-high> <height-low> IAC SE
 * Per RFC 1073, any data byte equal to IAC (255) is doubled.
 */
static void
send_naws(int fd)
{
  struct winsize ws;
  uchar data[4];
  uchar pkt[13]; /* 3 header + up to 8 escaped data bytes + 2 trailer */
  int n;
  int k;
  ushort w;
  ushort h;

  if(ioctl(0, TIOCGWINSZ, &ws) < 0) {
    ws.ws_col = 80;
    ws.ws_row = 24;
  }
  w = ws.ws_col ? ws.ws_col : 80;
  h = ws.ws_row ? ws.ws_row : 24;

  data[0] = (uchar)(w >> 8);
  data[1] = (uchar)(w & 0xff);
  data[2] = (uchar)(h >> 8);
  data[3] = (uchar)(h & 0xff);

  pkt[0] = IAC;
  pkt[1] = SB;
  pkt[2] = OPT_NAWS;
  n = 3;
  for(k = 0; k < 4; k++) {
    pkt[n++] = data[k];
    if(data[k] == IAC)
      pkt[n++] = (uchar)IAC; /* escape IAC in SB data */
  }
  pkt[n++] = IAC;
  pkt[n++] = SE;
  send(fd, pkt, n);
}

static int
process_telnet_rx(int fd, char *in, int n, char *out)
{
  int i;
  int j;
  int in_sb;
  uchar b;

  i = 0;
  j = 0;
  in_sb = 0;

  while(i < n) {
    b = (uchar)in[i++];

    if(in_sb) {
      if(b == IAC && i < n && (uchar)in[i] == SE) {
        i++;
        in_sb = 0;
      }
      continue;
    }

    if(b != IAC) {
      out[j++] = (char)b;
      continue;
    }

    if(i >= n)
      break;
    b = (uchar)in[i++];

    if(b == IAC) {
      out[j++] = (char)IAC;
      continue;
    }

    if(b == DO || b == DONT || b == WILL || b == WONT) {
      uchar opt;
      if(i >= n)
        break;
      opt = (uchar)in[i++];
      if(b == DO && opt == OPT_NAWS) {
        /* Server agrees to receive our window size; send SB NAWS */
        naws_enabled = 1;
        send_naws(fd);
      } else if(b == DONT && opt == OPT_NAWS) {
        naws_enabled = 0;
      } else if(b == DO) {
        send_iac_reply(fd, WONT, opt);
      } else if(b == WILL) {
        send_iac_reply(fd, DONT, opt);
      }
      continue;
    }

    if(b == SB) {
      in_sb = 1;
      continue;
    }
  }

  return j;
}

static int
stdin_raw_enable(struct termios *oldt)
{
  struct termios t;

  if(tcgetattr(0, oldt) < 0)
    return -1;
  t = *oldt;
  t.c_lflag &= ~(ECHO | ICANON);
  if(tcsetattr(0, TCSANOW, &t) < 0)
    return -1;
  return 0;
}

static int
is_local_escape(uchar c)
{
  return c == TELNET_ESCAPE || c == TELNET_CTRL_C || c == TELNET_CTRL_Z;
}

int
main(int argc, char **argv)
{
  int fd;
  int port;
  int pid;
  int n;
  int st;
  int wr;
  int raw_ok;
  char buf[TELNET_BUF];
  char out[TELNET_BUF];
  struct termios oldt;

  if(argc != 2 && argc != 3)
    usage();

  port = TELNET_DEFAULT_PORT;
  if(argc == 3) {
    port = parse_port(argv[2]);
    if(port < 0) {
      dprintf(2, "telnet: invalid port\n");
      exit(0);
    }
  }

  fd = open_telnet(argv[1], port);
  if(fd < 0)
    exit(0);

  /* Proactively offer NAWS so the server can ask for our window size */
  send_iac_reply(fd, WILL, OPT_NAWS);

  raw_ok = (stdin_raw_enable(&oldt) == 0);
  if(raw_ok)
    dprintf(2, "telnet: connected (Ctrl-], Ctrl-C, or Ctrl-Z to quit)\n");

  pid = fork();
  if(pid < 0) {
    dprintf(2, "telnet: fork failed\n");
    if(raw_ok)
      tcsetattr(0, TCSANOW, &oldt);
    close(fd);
    exit(0);
  }

  if(pid == 0) {
    char c;
    uchar tx[2];

    while(read(0, &c, 1) == 1) {
      if(is_local_escape((uchar)c))
        break;

      if(c == '\n') {
        tx[0] = '\r';
        tx[1] = '\n';
        if(send(fd, tx, 2) < 0)
          break;
        continue;
      }

      if((uchar)c == IAC) {
        tx[0] = IAC;
        tx[1] = IAC;
        if(send(fd, tx, 2) < 0)
          break;
        continue;
      }

      if(send(fd, &c, 1) < 0)
        break;
    }
    close(fd);
    exit(0);
  }

  /* Parent: install SIGWINCH handler to track window size changes */
  signal(SIGWINCH, sigwinch_handler);

  for(;;) {
    if(waitpid(pid, &st, WNOHANG) > 0)
      break;

    n = recvtimeout(fd, buf, sizeof(buf), TELNET_RECV_POLL_TICKS);
    if(n == RECV_TIMEOUT_EXPIRED) {
      if(naws_enabled && naws_dirty) {
        naws_dirty = 0;
        send_naws(fd);
      }
      continue;
    }

    if(n <= 0)
      break;

    n = process_telnet_rx(fd, buf, n, out);
    if(n > 0) {
      wr = write(1, out, n);
      if(wr != n)
        break;
    }
  }

  if(waitpid(pid, &st, WNOHANG) == 0) {
    kill(pid, SIGTERM);
    waitpid(pid, &st, 0);
  }

  if(raw_ok)
    tcsetattr(0, TCSANOW, &oldt);
  close(fd);
  exit(0);
}
