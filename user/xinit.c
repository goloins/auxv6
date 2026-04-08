#include "types.h"
#include "auxv6/user.h"
#include "socket.h"
#include "fcntl.h"
#include "stdio.h"
#include "stdlib.h"
#include "pwd.h"

#define X6_DEFAULT_PORT 6006
#define XINIT_READY_RETRIES 20

static char xinitrc_path[128];
static int client_path_exists(const char *path);

static int
resolve_xinitrc(char *out, int outsz)
{
  char cand[128];
  char *home;
  struct passwd *pw;

  if(!out || outsz <= 0)
    return -1;

  home = getenv("HOME");
  if((home == 0 || home[0] == 0)) {
    pw = getpwuid(getuid());
    if(pw && pw->pw_dir && pw->pw_dir[0])
      home = pw->pw_dir;
  }

  if(home && home[0]) {
    snprintf(cand, sizeof(cand), "%s/.xinitrc", home);
    if(client_path_exists(cand)) {
      snprintf(out, outsz, "%s", cand);
      return 0;
    }
  }

  if(client_path_exists("/.xinitrc")) {
    snprintf(out, outsz, "/.xinitrc");
    return 0;
  }

  if(client_path_exists("/etc/xinitrc")) {
    snprintf(out, outsz, "/etc/xinitrc");
    return 0;
  }

  out[0] = 0;
  return 0;
}

static void
usage(void)
{
  dprintf(2, "usage: xinit [client [args...]] [-- x6-args...]\n");
  dprintf(2, "examples:\n");
  dprintf(2, "  xinit /bin/dwm\n");
  dprintf(2, "  xinit /bin/dwm -- -p 6006\n");
  exit(1);
}

static int
probe_x6_ready(int port)
{
  int fd;
  struct sockaddr_in dst;
  char buf[160];
  int n;

  fd = socket(AF_INET, SOCK_STREAM, 0);
  if(fd < 0)
    return -1;

  memset(&dst, 0, sizeof(dst));
  dst.sin_family = AF_INET;
  dst.sin_port = (ushort)port;
  dst.sin_addr = INADDR_LOOPBACK;

  if(connect(fd, &dst, sizeof(dst)) < 0) {
    close(fd);
    return -1;
  }

  n = recv(fd, buf, sizeof(buf) - 1);
  if(n <= 0) {
    close(fd);
    return -1;
  }
  buf[n] = 0;

  send(fd, "HELLO x6/1\n", 11);
  n = recv(fd, buf, sizeof(buf) - 1);
  if(n <= 0)
  {
    close(fd);
    return -1;
  }
  buf[n] = 0;

  if(strncmp(buf, "OK proto=", 9) != 0) {
    close(fd);
    return -1;
  }

  send(fd, "DETACH\n", 7);
  n = recv(fd, buf, sizeof(buf) - 1);
  close(fd);
  if(n > 0) {
    buf[n] = 0;
    if(strncmp(buf, "BYE", 3) != 0)
      return -1;
  }
  return 0;
}

static int
client_path_exists(const char *path)
{
  int fd;

  if(!path || path[0] == 0)
    return 0;
  fd = open(path, O_RDONLY);
  if(fd < 0)
    return 0;
  close(fd);
  return 1;
}

static void
stop_x6_or_kill(int x6_pid)
{
  int i;
  int st;

  if(x6_pid <= 0)
    return;

  kill(x6_pid, SIGTERM);
  for(i = 0; i < 3; i++) {
    if(waitpid(x6_pid, &st, WNOHANG) == x6_pid)
      return;
    sleep(1);
  }

  kill(x6_pid, SIGKILL);
  waitpid(x6_pid, 0, 0);
}

int
main(int argc, char **argv)
{
  char *default_client[] = { "/bin/dash", 0 };
  char *xinitrc_client[3];
  char *x6_argv[24];
  char **client_argv;
  int x6_argc;
  int client_idx;
  int x6_pid;
  int client_pid;
  int port;
  int i;
  int ready;
  int st;
  int using_xinitrc;

  client_idx = 1;
  x6_argc = 0;
  port = X6_DEFAULT_PORT;
  using_xinitrc = 0;

  if(argc > 1 && strcmp(argv[1], "-h") == 0)
    usage();

  for(i = 1; i < argc; i++) {
    if(strcmp(argv[i], "--") == 0)
      break;
  }

  if(i < argc) {
    int j;
    for(j = i + 1; j < argc && x6_argc < (int)(sizeof(x6_argv) / sizeof(x6_argv[0])) - 3; j++) {
      x6_argv[x6_argc++] = argv[j];
      if(strcmp(argv[j], "-p") == 0 && j + 1 < argc)
        port = atoi(argv[j + 1]);
    }
    argc = i;
  }

  if(argc > 1)
    client_argv = &argv[client_idx];
  else {
    resolve_xinitrc(xinitrc_path, sizeof(xinitrc_path));
    if(xinitrc_path[0]) {
      xinitrc_client[0] = "/bin/dash";
      xinitrc_client[1] = xinitrc_path;
      xinitrc_client[2] = 0;
      client_argv = xinitrc_client;
      using_xinitrc = 1;
    } else {
      client_argv = default_client;
    }
  }

  if(using_xinitrc)
    dprintf(1, "xinit: using xinitrc %s\n", xinitrc_path);

  if(client_argv[0] && client_argv[0][0] == '/' && !client_path_exists(client_argv[0])) {
    dprintf(2, "xinit: client not found: %s\n", client_argv[0]);
    return 1;
  }

  if(client_argv[1] && client_argv[1][0] == '/' && !client_path_exists(client_argv[1])) {
    dprintf(2, "xinit: client argument not found: %s\n", client_argv[1]);
    return 1;
  }

  dprintf(1, "xinit: launching client %s\n", client_argv[0] ? client_argv[0] : "(null)");

  if(port < 1 || port > 65535) {
    dprintf(2, "xinit: invalid x6 port\n");
    return 1;
  }

  x6_pid = fork();
  if(x6_pid < 0) {
    dprintf(2, "xinit: fork for x6 failed\n");
    return 1;
  }

  if(x6_pid == 0) {
    char *exec_argv[32];
    int e = 0;

    exec_argv[e++] = "/bin/x6";
    exec_argv[e++] = "-f";
    for(i = 0; i < x6_argc && e < (int)(sizeof(exec_argv) / sizeof(exec_argv[0])) - 1; i++)
      exec_argv[e++] = x6_argv[i];
    exec_argv[e] = 0;
    exec(exec_argv[0], exec_argv);
    dprintf(2, "xinit: exec failed for /bin/x6\n");
    exit(1);
  }

  ready = -1;
  for(i = 0; i < XINIT_READY_RETRIES; i++) {
    if(probe_x6_ready(port) == 0) {
      ready = 0;
      break;
    }
    sleep(1);
    if(waitpid(x6_pid, &st, WNOHANG) == x6_pid)
      break;
  }

  if(ready < 0) {
    dprintf(2, "xinit: x6 did not become ready\n");
    stop_x6_or_kill(x6_pid);
    return 1;
  }

  client_pid = fork();
  if(client_pid < 0) {
    dprintf(2, "xinit: fork for client failed\n");
    stop_x6_or_kill(x6_pid);
    return 1;
  }

  if(client_pid == 0) {
    dprintf(1, "xinit: exec client now: %s\n", client_argv[0]);
    exec(client_argv[0], client_argv);
    dprintf(2, "xinit: exec failed for %s\n", client_argv[0]);
    exit(1);
  }

  dprintf(1, "xinit: client pid=%d\n", client_pid);

  if(waitpid(client_pid, &st, 0) < 0)
    st = 1;

  if(WIFEXITED(st))
    dprintf(1, "xinit: client exited code=%d\n", WEXITSTATUS(st));
  else if(WIFSIGNALED(st))
    dprintf(1, "xinit: client signaled sig=%d\n", WTERMSIG(st));
  else
    dprintf(1, "xinit: client exited status=%d\n", st);

  stop_x6_or_kill(x6_pid);

  return st;
}
