#include "types.h"
#include "auxv6/user.h"
#include "fcntl.h"
#include "signal.h"
#include "socket.h"

#define SERVER7_DEFAULT_PORT 6007
#define SERVER7_BACKLOG      8
#define SERVER7_PROTO_VERSION 1

#define SERVER7_PROC_PATH "/proc/server7"

#define SERVER7_FLOW_DESKTOP_DIRECT 1
#define SERVER7_FLOW_LOGIN_DIALOG   2

static volatile sig_atomic_t keep_running = 1;

static int server7_flow = SERVER7_FLOW_LOGIN_DIALOG;
static int server7_has_tty;
static int server7_uid;

static const char *
server7_flow_name(int flow)
{
  if(flow == SERVER7_FLOW_DESKTOP_DIRECT)
    return "desktop-direct";
  return "login-dialog";
}

static void
usage(void)
{
  dprintf(2, "usage: server7 [-f] [-p port] [-m desktop|login]\n");
  dprintf(2, "       -f   run in foreground (no daemonize)\n");
  dprintf(2, "       -p   listen port (default %d)\n", SERVER7_DEFAULT_PORT);
  dprintf(2, "       -m   startup flow override: desktop or login\n");
  exit(1);
}

static int
daemonize_self(void)
{
  int pid;
  int fd;

  pid = fork();
  if(pid < 0)
    return -1;
  if(pid > 0)
    exit(0);

  if(setsid() < 0)
    return -1;

  pid = fork();
  if(pid < 0)
    return -1;
  if(pid > 0)
    exit(0);

  chdir("/");

  close(0);
  close(1);
  close(2);

  fd = open("/dev/console", O_RDWR);
  if(fd < 0)
    return 0;

  if(fd != 0) {
    dup2(fd, 0);
    close(fd);
  }
  dup(0);
  dup(0);

  return 0;
}

static void
on_term(int signo)
{
  if(signo == SIGTERM || signo == SIGINT)
    keep_running = 0;
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
parse_flow_mode(const char *s)
{
  if(s == 0)
    return -1;
  if(strcmp(s, "desktop") == 0)
    return SERVER7_FLOW_DESKTOP_DIRECT;
  if(strcmp(s, "login") == 0)
    return SERVER7_FLOW_LOGIN_DIALOG;
  return -1;
}

static int
has_authenticated_tty_session(void)
{
  if(isatty(0) || isatty(1) || isatty(2))
    return 1;
  return 0;
}

static int
choose_startup_flow(int flow_override)
{
  if(flow_override > 0)
    return flow_override;

  server7_uid = (int)getuid();
  server7_has_tty = has_authenticated_tty_session();

  if(server7_uid > 0 && server7_has_tty)
    return SERVER7_FLOW_DESKTOP_DIRECT;
  return SERVER7_FLOW_LOGIN_DIALOG;
}

static void
server7_copy_text(char *dst, const char *src, int dstlen)
{
  int i;

  if(dst == 0 || src == 0 || dstlen <= 0)
    return;

  for(i = 0; i < dstlen - 1 && src[i]; i++)
    dst[i] = src[i];
  dst[i] = 0;
}

static int
server7_proc_write(const char *cmd)
{
  int fd;
  int n;

  fd = open(SERVER7_PROC_PATH, O_RDWR);
  if(fd < 0)
    return -1;

  n = strlen(cmd);
  if(write(fd, (char *)cmd, n) != n) {
    close(fd);
    return -1;
  }

  close(fd);
  return 0;
}

static int
server7_claim_display(void)
{
  return server7_proc_write("claim\n");
}

static void
server7_release_display(void)
{
  server7_proc_write("release\n");
}

static int
server7_read_status(char *buf, int buflen)
{
  int fd;
  int n;

  if(buf == 0 || buflen <= 1)
    return -1;

  fd = open(SERVER7_PROC_PATH, O_RDONLY);
  if(fd < 0)
    return -1;

  n = read(fd, buf, buflen - 1);
  close(fd);
  if(n < 0)
    return -1;
  buf[n] = 0;
  return n;
}

static void
server7_status_compact(char *out, int outlen)
{
  char status[256];
  int i;
  int j;

  if(out == 0 || outlen <= 0)
    return;

  if(server7_read_status(status, sizeof(status)) < 0) {
    server7_copy_text(out, "status=unavailable", outlen);
    return;
  }

  j = 0;
  for(i = 0; status[i] && j < outlen - 1; i++) {
    char c;

    c = status[i];
    if(c == '\n' || c == '\r' || c == '\t')
      c = ' ';
    if(c == ' ' && j > 0 && out[j - 1] == ' ')
      continue;
    out[j++] = c;
  }
  if(j > 0 && out[j - 1] == ' ')
    j--;
  out[j] = 0;
}

static void
server7_send_client_line(int cfd, const char *s)
{
  if(cfd < 0 || s == 0)
    return;
  send(cfd, (void *)s, strlen(s));
}

static int
server7_recv_line(int cfd, char *buf, int buflen)
{
  int n;

  if(cfd < 0 || buf == 0 || buflen <= 1)
    return -1;

  n = recv(cfd, buf, buflen - 1);
  if(n <= 0)
    return -1;
  buf[n] = 0;
  return n;
}

static void
server7_handle_client(int cfd)
{
  char req[160];
  char status[320];

  server7_send_client_line(cfd, "SERVER7/1 READY\n");

  if(server7_recv_line(cfd, req, sizeof(req)) < 0)
    return;

  if(strncmp(req, "HELLO server7/1", 15) == 0) {
    server7_status_compact(status, sizeof(status));
    dprintf(cfd,
            "OK proto=%d flow=%s caps=claim,menu,wm,input-kbd,input-mouse %s\n",
            SERVER7_PROTO_VERSION, server7_flow_name(server7_flow), status);
    return;
  }

  if(strncmp(req, "STATUS", 6) == 0) {
    int n;

    dprintf(cfd, "flow %s\n", server7_flow_name(server7_flow));
    dprintf(cfd, "uid %d tty %d\n", server7_uid, server7_has_tty);

    n = server7_read_status(status, sizeof(status));
    if(n < 0)
      server7_send_client_line(cfd, "ERR status unavailable\n");
    else
      send(cfd, status, n);
    return;
  }

  if(strncmp(req, "PING", 4) == 0) {
    server7_send_client_line(cfd, "PONG\n");
    return;
  }

  server7_send_client_line(cfd, "ERR unknown request\n");
}

int
main(int argc, char **argv)
{
  int i;
  int foreground;
  int flow_override;
  int port;
  int fd;
  struct sockaddr_in src;
  struct sigaction sa;

  foreground = 0;
  flow_override = -1;
  port = SERVER7_DEFAULT_PORT;

  for(i = 1; i < argc; i++) {
    if(strcmp(argv[i], "-f") == 0) {
      foreground = 1;
      continue;
    }
    if(strcmp(argv[i], "-p") == 0) {
      if(i + 1 >= argc)
        usage();
      port = parse_port(argv[++i]);
      if(port < 0)
        usage();
      continue;
    }
    if(strcmp(argv[i], "-m") == 0) {
      if(i + 1 >= argc)
        usage();
      flow_override = parse_flow_mode(argv[++i]);
      if(flow_override < 0)
        usage();
      continue;
    }
    usage();
  }

  server7_flow = choose_startup_flow(flow_override);
  if(server7_flow == SERVER7_FLOW_DESKTOP_DIRECT && !foreground)
    foreground = 1;

  if(!foreground) {
    if(daemonize_self() < 0) {
      dprintf(2, "server7: daemonize failed\n");
      exit(1);
    }
  }

  memset(&sa, 0, sizeof(sa));
  sa.sa_handler = on_term;
  sigaction(SIGTERM, &sa, 0);
  sigaction(SIGINT, &sa, 0);

  if(server7_claim_display() < 0) {
    dprintf(2, "server7: display claim failed via %s\n", SERVER7_PROC_PATH);
    exit(1);
  }

  fd = socket(AF_INET, SOCK_STREAM, 0);
  if(fd < 0) {
    server7_release_display();
    dprintf(2, "server7: socket failed\n");
    exit(1);
  }

  memset(&src, 0, sizeof(src));
  src.sin_family = AF_INET;
  src.sin_port = (ushort)port;
  src.sin_addr = INADDR_LOOPBACK;

  if(bind(fd, &src, sizeof(src)) < 0) {
    dprintf(2, "server7: bind failed on 127.0.0.1:%d\n", port);
    close(fd);
    server7_release_display();
    exit(1);
  }

  if(listen(fd, SERVER7_BACKLOG) < 0) {
    dprintf(2, "server7: listen failed\n");
    close(fd);
    server7_release_display();
    exit(1);
  }

  if(server7_flow == SERVER7_FLOW_DESKTOP_DIRECT) {
    dprintf(1, "server7: startup flow=desktop-direct (authenticated tty user)\n");
    dprintf(1, "server7: drawing desktop directly\n");
  } else {
    dprintf(1, "server7: startup flow=login-dialog (init or system session)\n");
    dprintf(1, "server7: presenting A/UX-style login dialog path\n");
  }

  dprintf(1, "server7: active proto=%d on 127.0.0.1:%d\n",
          SERVER7_PROTO_VERSION, port);

  while(keep_running) {
    int cfd;

    cfd = accept(fd);
    if(cfd < 0)
      continue;

    server7_handle_client(cfd);
    close(cfd);
  }

  close(fd);
  server7_release_display();
  dprintf(1, "server7: exiting\n");
  return 0;
}
