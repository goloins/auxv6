#include "../include/types.h"
#include "../include/user.h"
#include "../include/socket.h"
#include "../include/termios.h"

#define TELNET_DEFAULT_PORT 23
#define TELNET_BUF 512

#define IAC  255
#define DONT 254
#define DO   253
#define WONT 252
#define WILL 251
#define SB   250
#define SE   240

#define TELNET_ESCAPE 29  /* Ctrl-] */
#define TELNET_CTRL_C 3
#define TELNET_CTRL_Z 26
#define TELNET_RECV_POLL_TICKS 10

static void
usage(void)
{
  printf(2, "usage: telnet host [port]\n");
  exit();
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
    printf(2, "telnet: cannot resolve host %s\n", host);
    return -1;
  }

  fd = socket(AF_INET, SOCK_STREAM, 0);
  if(fd < 0) {
    printf(2, "telnet: socket failed\n");
    return -1;
  }

  memset(&dst, 0, sizeof(dst));
  dst.sin_family = AF_INET;
  dst.sin_port = (ushort)port;
  dst.sin_addr = ip;

  if(connect(fd, &dst, sizeof(dst)) < 0) {
    printf(2, "telnet: connect failed\n");
    close(fd);
    return -1;
  }

  return fd;
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
      if(b == DO)
        send_iac_reply(fd, WONT, opt);
      else if(b == WILL)
        send_iac_reply(fd, DONT, opt);
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
      printf(2, "telnet: invalid port\n");
      exit();
    }
  }

  fd = open_telnet(argv[1], port);
  if(fd < 0)
    exit();

  raw_ok = (stdin_raw_enable(&oldt) == 0);
  if(raw_ok)
    printf(2, "telnet: connected (Ctrl-], Ctrl-C, or Ctrl-Z to quit)\n");

  pid = fork();
  if(pid < 0) {
    printf(2, "telnet: fork failed\n");
    if(raw_ok)
      tcsetattr(0, TCSANOW, &oldt);
    close(fd);
    exit();
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
    exit();
  }

  for(;;) {
    if(waitpid(pid, &st, WNOHANG) > 0)
      break;

    n = recvtimeout(fd, buf, sizeof(buf), TELNET_RECV_POLL_TICKS);
    if(n < 0)
      break;

    if(n == 0) {
      continue;
    }

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
  exit();
}
