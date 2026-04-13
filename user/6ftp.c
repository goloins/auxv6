#include "types.h"
#include "auxv6/user.h"
#include "socket.h"
#include "fcntl.h"
#include "stdio.h"
#include "string.h"
#include "stdarg.h"

#define FTP_DEFAULT_PORT 21
#define FTP_HOST_MAX 127
#define FTP_USER_MAX 63
#define FTP_PASS_MAX 127
#define FTP_LINE_MAX 512
#define FTP_CMD_MAX 512
#define FTP_PATH_MAX 255
#define FTP_IO_BUF 1024

struct ftp_client {
  int ctrl_fd;
  uint server_ip;
  char host[FTP_HOST_MAX + 1];
  int port;
  char user[FTP_USER_MAX + 1];
  char transfer_type;
  int logged_in;
};

static void usage(void);
static int is_digit(char c);
static int is_space(char c);
static void trim_line(char *s);
static int parse_port(const char *s);
static int send_all(int fd, const void *buf, int len);
static int write_all(int fd, const void *buf, int len);
static int open_tcp(uint ip, int port);
static int recv_line(int fd, char *buf, int bufsz);
static int ftp_read_reply(struct ftp_client *client, char *out, int outsz, int *code_out);
static int ftp_command(struct ftp_client *client, char *reply, int replysz, int *code_out, const char *fmt, ...);
static int ftp_set_type(struct ftp_client *client, char type);
static int ftp_open_pasv(struct ftp_client *client);
static int ftp_print_listing(struct ftp_client *client, const char *path);
static int ftp_get_file(struct ftp_client *client, const char *remote, const char *local);
static int ftp_put_file(struct ftp_client *client, const char *local, const char *remote);
static int ftp_login(struct ftp_client *client, const char *username, const char *password, int allow_prompt);
static void prompt_username(char *user, int usersz, const char *host);
static int prompt_password(char *pass, int passsz, const char *user);
static int default_leaf_name(const char *path, char *out, int outsz);
static int split_words(char *line, char **argv, int maxargv);
static char *skip_word(char *s);
static void print_help(void);
static int run_shell(struct ftp_client *client, int interactive);

static void
usage(void)
{
  dprintf(2, "usage: 6ftp [-u user] [-p password] host [port]\n");
  exit(1);
}

static int
is_digit(char c)
{
  return c >= '0' && c <= '9';
}

static int
is_space(char c)
{
  return c == ' ' || c == '\t' || c == '\r' || c == '\n';
}

static void
trim_line(char *s)
{
  int n;

  if(s == 0)
    return;
  n = strlen(s);
  while(n > 0 && (s[n - 1] == '\r' || s[n - 1] == '\n'))
    s[--n] = 0;
}

static int
parse_port(const char *s)
{
  int port;

  if(s == 0 || s[0] == 0)
    return -1;
  port = atoi(s);
  if(port < 1 || port > 65535)
    return -1;
  return port;
}

static int
send_all(int fd, const void *buf, int len)
{
  const char *p;
  int off;
  int n;

  p = (const char *)buf;
  off = 0;
  while(off < len) {
    n = send(fd, p + off, len - off);
    if(n <= 0)
      return -1;
    off += n;
  }
  return 0;
}

static int
write_all(int fd, const void *buf, int len)
{
  const char *p;
  int off;
  int n;

  p = (const char *)buf;
  off = 0;
  while(off < len) {
    n = write(fd, p + off, len - off);
    if(n <= 0)
      return -1;
    off += n;
  }
  return 0;
}

static int
open_tcp(uint ip, int port)
{
  int fd;
  struct sockaddr_in addr;

  fd = socket(AF_INET, SOCK_STREAM, 0);
  if(fd < 0)
    return -1;

  memset(&addr, 0, sizeof(addr));
  addr.sin_family = AF_INET;
  addr.sin_port = (ushort)port;
  addr.sin_addr.s_addr = ip;

  if(connect(fd, &addr, sizeof(addr)) < 0) {
    close(fd);
    return -1;
  }

  return fd;
}

static int
recv_line(int fd, char *buf, int bufsz)
{
  int n;
  int rc;
  char ch;
  int truncated;

  if(bufsz < 2)
    return -1;

  n = 0;
  truncated = 0;
  for(;;) {
    rc = recv(fd, &ch, 1);
    if(rc <= 0) {
      if(n == 0)
        return -1;
      break;
    }
    if(ch == '\r')
      continue;
    if(!truncated && n < bufsz - 1)
      buf[n++] = ch;
    else
      truncated = 1;
    if(ch == '\n')
      break;
  }
  buf[n] = 0;
  return n;
}

static int
ftp_read_reply(struct ftp_client *client, char *out, int outsz, int *code_out)
{
  char line[FTP_LINE_MAX];
  int code;
  int multiline;
  int got_code;

  got_code = 0;
  code = -1;
  multiline = 0;
  if(out && outsz > 0)
    out[0] = 0;

  for(;;) {
    if(recv_line(client->ctrl_fd, line, sizeof(line)) < 0)
      return -1;
    trim_line(line);
    if(line[0])
      dprintf(1, "%s\n", line);

    if(is_digit(line[0]) && is_digit(line[1]) && is_digit(line[2])) {
      int cur;

      cur = (line[0] - '0') * 100 + (line[1] - '0') * 10 + (line[2] - '0');
      if(!got_code) {
        code = cur;
        got_code = 1;
        multiline = (line[3] == '-');
        if(out && outsz > 0) {
          snprintf(out, outsz, "%s", line);
          out[outsz - 1] = 0;
        }
        if(!multiline)
          break;
      } else {
        if(out && outsz > 0) {
          snprintf(out, outsz, "%s", line);
          out[outsz - 1] = 0;
        }
        if(cur == code && line[3] == ' ')
          break;
      }
    }
  }

  if(code_out)
    *code_out = code;
  return 0;
}

static int
ftp_command(struct ftp_client *client, char *reply, int replysz, int *code_out, const char *fmt, ...)
{
  char cmd[FTP_CMD_MAX];
  va_list ap;
  int n;

  va_start(ap, fmt);
  n = vsnprintf(cmd, sizeof(cmd), fmt, ap);
  va_end(ap);
  if(n < 0 || n >= (int)sizeof(cmd)) {
    dprintf(2, "6ftp: command too long\n");
    return -1;
  }
  if(send_all(client->ctrl_fd, cmd, n) < 0 || send_all(client->ctrl_fd, "\r\n", 2) < 0) {
    dprintf(2, "6ftp: control send failed\n");
    return -1;
  }
  return ftp_read_reply(client, reply, replysz, code_out);
}

static int
ftp_set_type(struct ftp_client *client, char type)
{
  char reply[FTP_LINE_MAX];
  int code;

  if(type != 'A' && type != 'I')
    return -1;
  if(ftp_command(client, reply, sizeof(reply), &code, "TYPE %c", type) < 0)
    return -1;
  if(code / 100 != 2)
    return -1;
  client->transfer_type = type;
  return 0;
}

static int
ftp_open_pasv(struct ftp_client *client)
{
  char reply[FTP_LINE_MAX];
  char *p;
  int code;
  int h1;
  int h2;
  int h3;
  int h4;
  int p1;
  int p2;
  int port;

  if(ftp_command(client, reply, sizeof(reply), &code, "PASV") < 0)
    return -1;
  if(code != 227) {
    dprintf(2, "6ftp: PASV failed\n");
    return -1;
  }

  p = strchr(reply, '(');
  if(p == 0 || sscanf(p, "(%d,%d,%d,%d,%d,%d)", &h1, &h2, &h3, &h4, &p1, &p2) != 6) {
    dprintf(2, "6ftp: malformed PASV reply\n");
    return -1;
  }
  if(h1 < 0 || h1 > 255 || h2 < 0 || h2 > 255 || h3 < 0 || h3 > 255 || h4 < 0 || h4 > 255 ||
     p1 < 0 || p1 > 255 || p2 < 0 || p2 > 255) {
    dprintf(2, "6ftp: invalid PASV address\n");
    return -1;
  }

  port = p1 * 256 + p2;
  return open_tcp(client->server_ip, port);
}

static int
ftp_print_listing(struct ftp_client *client, const char *path)
{
  char reply[FTP_LINE_MAX];
  char buf[FTP_IO_BUF];
  int code;
  int datafd;
  int n;

  datafd = ftp_open_pasv(client);
  if(datafd < 0)
    return -1;

  if(path && path[0]) {
    if(ftp_command(client, reply, sizeof(reply), &code, "LIST %s", path) < 0) {
      close(datafd);
      return -1;
    }
  } else {
    if(ftp_command(client, reply, sizeof(reply), &code, "LIST") < 0) {
      close(datafd);
      return -1;
    }
  }
  if(code != 125 && code != 150) {
    close(datafd);
    return -1;
  }

  while((n = recv(datafd, buf, sizeof(buf))) > 0) {
    if(write_all(1, buf, n) < 0) {
      close(datafd);
      return -1;
    }
  }
  close(datafd);
  if(n < 0)
    return -1;

  if(ftp_read_reply(client, reply, sizeof(reply), &code) < 0)
    return -1;
  return (code / 100 == 2) ? 0 : -1;
}

static int
default_leaf_name(const char *path, char *out, int outsz)
{
  const char *base;
  const char *end;
  int len;

  if(path == 0 || out == 0 || outsz < 2)
    return -1;

  end = path + strlen(path);
  while(end > path && end[-1] == '/')
    end--;
  if(end == path)
    return -1;

  base = end;
  while(base > path && base[-1] != '/')
    base--;

  len = end - base;
  if(len <= 0 || len >= outsz)
    return -1;

  memmove(out, base, len);
  out[len] = 0;
  return 0;
}

static int
ftp_get_file(struct ftp_client *client, const char *remote, const char *local)
{
  char reply[FTP_LINE_MAX];
  char local_path[FTP_PATH_MAX + 1];
  char buf[FTP_IO_BUF];
  int code;
  int datafd;
  int outfd;
  int n;
  int ok;

  if(remote == 0 || remote[0] == 0)
    return -1;
  if(local == 0 || local[0] == 0) {
    if(default_leaf_name(remote, local_path, sizeof(local_path)) < 0) {
      dprintf(2, "6ftp: cannot infer local filename for %s\n", remote);
      return -1;
    }
    local = local_path;
  }

  if(ftp_set_type(client, 'I') < 0) {
    dprintf(2, "6ftp: failed to enable binary mode\n");
    return -1;
  }

  datafd = ftp_open_pasv(client);
  if(datafd < 0)
    return -1;

  if(ftp_command(client, reply, sizeof(reply), &code, "RETR %s", remote) < 0) {
    close(datafd);
    return -1;
  }
  if(code != 125 && code != 150) {
    close(datafd);
    return -1;
  }

  outfd = open(local, O_CREATE | O_WRONLY | O_TRUNC);
  if(outfd < 0) {
    dprintf(2, "6ftp: cannot open %s for writing\n", local);
    close(datafd);
    ftp_read_reply(client, reply, sizeof(reply), &code);
    return -1;
  }

  ok = 1;
  while((n = recv(datafd, buf, sizeof(buf))) > 0) {
    if(write_all(outfd, buf, n) < 0) {
      dprintf(2, "6ftp: write failed for %s\n", local);
      ok = 0;
      break;
    }
  }
  if(n < 0)
    ok = 0;

  close(outfd);
  close(datafd);

  if(ftp_read_reply(client, reply, sizeof(reply), &code) < 0)
    ok = 0;
  else if(code / 100 != 2)
    ok = 0;

  if(!ok) {
    unlink(local);
    return -1;
  }
  return 0;
}

static int
ftp_put_file(struct ftp_client *client, const char *local, const char *remote)
{
  char reply[FTP_LINE_MAX];
  char remote_path[FTP_PATH_MAX + 1];
  char buf[FTP_IO_BUF];
  int code;
  int datafd;
  int infd;
  int n;
  int ok;

  if(local == 0 || local[0] == 0)
    return -1;
  if(remote == 0 || remote[0] == 0) {
    if(default_leaf_name(local, remote_path, sizeof(remote_path)) < 0) {
      dprintf(2, "6ftp: cannot infer remote filename for %s\n", local);
      return -1;
    }
    remote = remote_path;
  }

  infd = open(local, O_RDONLY);
  if(infd < 0) {
    dprintf(2, "6ftp: cannot open %s\n", local);
    return -1;
  }

  if(ftp_set_type(client, 'I') < 0) {
    dprintf(2, "6ftp: failed to enable binary mode\n");
    close(infd);
    return -1;
  }

  datafd = ftp_open_pasv(client);
  if(datafd < 0) {
    close(infd);
    return -1;
  }

  if(ftp_command(client, reply, sizeof(reply), &code, "STOR %s", remote) < 0) {
    close(datafd);
    close(infd);
    return -1;
  }
  if(code != 125 && code != 150) {
    close(datafd);
    close(infd);
    return -1;
  }

  ok = 1;
  while((n = read(infd, buf, sizeof(buf))) > 0) {
    if(send_all(datafd, buf, n) < 0) {
      dprintf(2, "6ftp: send failed during upload\n");
      ok = 0;
      break;
    }
  }
  if(n < 0)
    ok = 0;

  close(infd);
  close(datafd);

  if(ftp_read_reply(client, reply, sizeof(reply), &code) < 0)
    ok = 0;
  else if(code / 100 != 2)
    ok = 0;

  return ok ? 0 : -1;
}

static void
prompt_username(char *user, int usersz, const char *host)
{
  char line[FTP_USER_MAX + 8];

  dprintf(1, "Name (%s:anonymous): ", host);
  fflush(stdout);
  if(fgets(line, sizeof(line), stdin) == 0) {
    snprintf(user, usersz, "anonymous");
    return;
  }
  trim_line(line);
  if(line[0] == 0)
    snprintf(user, usersz, "anonymous");
  else
    snprintf(user, usersz, "%s", line);
  user[usersz - 1] = 0;
}

static int
prompt_password(char *pass, int passsz, const char *user)
{
  dprintf(1, "Password for %s: ", user);
  fflush(stdout);
  if(readpass(pass, passsz) == 0)
    return -1;
  dprintf(1, "\n");
  return 0;
}

static int
ftp_login(struct ftp_client *client, const char *username, const char *password, int allow_prompt)
{
  char reply[FTP_LINE_MAX];
  char user[FTP_USER_MAX + 1];
  char pass[FTP_PASS_MAX + 1];
  int code;

  if(ftp_read_reply(client, reply, sizeof(reply), &code) < 0) {
    dprintf(2, "6ftp: failed to read server banner\n");
    return -1;
  }
  if(code != 220) {
    dprintf(2, "6ftp: unexpected server banner\n");
    return -1;
  }

  if(username && username[0]) {
    snprintf(user, sizeof(user), "%s", username);
  } else if(allow_prompt) {
    prompt_username(user, sizeof(user), client->host);
  } else {
    snprintf(user, sizeof(user), "anonymous");
  }
  user[sizeof(user) - 1] = 0;

  if(ftp_command(client, reply, sizeof(reply), &code, "USER %s", user) < 0)
    return -1;

  if(code == 230) {
    snprintf(client->user, sizeof(client->user), "%s", user);
    client->logged_in = 1;
    ftp_set_type(client, 'I');
    return 0;
  }
  if(code != 331) {
    dprintf(2, "6ftp: login rejected\n");
    return -1;
  }

  if(password && password[0]) {
    snprintf(pass, sizeof(pass), "%s", password);
  } else if(strcmp(user, "anonymous") == 0) {
    snprintf(pass, sizeof(pass), "anonymous@");
  } else if(allow_prompt) {
    if(prompt_password(pass, sizeof(pass), user) < 0)
      return -1;
  } else {
    dprintf(2, "6ftp: password required for %s; use -p in non-interactive mode\n", user);
    return -1;
  }
  pass[sizeof(pass) - 1] = 0;

  if(ftp_command(client, reply, sizeof(reply), &code, "PASS %s", pass) < 0)
    return -1;
  if(code != 230) {
    dprintf(2, "6ftp: authentication failed\n");
    return -1;
  }

  snprintf(client->user, sizeof(client->user), "%s", user);
  client->logged_in = 1;
  ftp_set_type(client, 'I');
  return 0;
}

static int
split_words(char *line, char **argv, int maxargv)
{
  int argc;
  char *tok;

  argc = 0;
  tok = strtok(line, " \t");
  while(tok && argc < maxargv) {
    argv[argc++] = tok;
    tok = strtok(0, " \t");
  }
  return argc;
}

static char *
skip_word(char *s)
{
  while(*s && !is_space(*s))
    s++;
  while(*s && is_space(*s))
    s++;
  return s;
}

static void
print_help(void)
{
  dprintf(1,
          "Commands:\n"
          "  ls [path]           list remote directory\n"
          "  pwd                 print remote working directory\n"
          "  cd <dir>            change remote directory\n"
          "  lcd <dir>           change local directory\n"
          "  lpwd                print local working directory\n"
          "  get <remote> [local] download file\n"
          "  put <local> [remote] upload file\n"
          "  mkdir <dir>         create remote directory\n"
          "  rmdir <dir>         remove remote directory\n"
          "  rm <path>           delete remote file\n"
          "  rename <old> <new>  rename remote path\n"
          "  binary              set TYPE I\n"
          "  ascii               set TYPE A\n"
          "  quote <command>     send raw FTP command\n"
          "  status              show session status\n"
          "  help                show this help\n"
          "  quit                close the session\n");
}

static int
run_shell(struct ftp_client *client, int interactive)
{
  char line[FTP_CMD_MAX];
  char work[FTP_CMD_MAX];
  char cwd[FTP_PATH_MAX + 1];
  char reply[FTP_LINE_MAX];
  char *argv[4];
  char *rest;
  int argc;
  int code;

  for(;;) {
    if(interactive) {
      dprintf(1, "ftp> ");
      fflush(stdout);
    }
    if(fgets(line, sizeof(line), stdin) == 0)
      break;

    trim_line(line);
    rest = line;
    while(*rest && is_space(*rest))
      rest++;
    if(*rest == 0)
      continue;

    snprintf(work, sizeof(work), "%s", rest);
    argc = split_words(work, argv, 4);
    if(argc == 0)
      continue;

    if(strcmp(argv[0], "help") == 0 || strcmp(argv[0], "?") == 0) {
      print_help();
      continue;
    }

    if(strcmp(argv[0], "quit") == 0 || strcmp(argv[0], "bye") == 0 || strcmp(argv[0], "exit") == 0) {
      ftp_command(client, reply, sizeof(reply), &code, "QUIT");
      return 0;
    }

    if(strcmp(argv[0], "status") == 0) {
      dprintf(1, "Connected to %s:%d as %s, transfer mode %c\n",
              client->host, client->port,
              client->user[0] ? client->user : "(unknown)",
              client->transfer_type ? client->transfer_type : '?');
      continue;
    }

    if(strcmp(argv[0], "pwd") == 0) {
      ftp_command(client, reply, sizeof(reply), &code, "PWD");
      continue;
    }

    if(strcmp(argv[0], "cd") == 0 || strcmp(argv[0], "cwd") == 0) {
      if(argc < 2) {
        dprintf(2, "6ftp: cd requires a remote directory\n");
        continue;
      }
      ftp_command(client, reply, sizeof(reply), &code, "CWD %s", argv[1]);
      continue;
    }

    if(strcmp(argv[0], "lcd") == 0) {
      if(argc < 2) {
        dprintf(2, "6ftp: lcd requires a local directory\n");
        continue;
      }
      if(chdir(argv[1]) < 0)
        dprintf(2, "6ftp: local chdir failed for %s\n", argv[1]);
      continue;
    }

    if(strcmp(argv[0], "lpwd") == 0) {
      if(getcwd(cwd, sizeof(cwd)) == 0)
        dprintf(2, "6ftp: getcwd failed\n");
      else
        dprintf(1, "%s\n", cwd);
      continue;
    }

    if(strcmp(argv[0], "ls") == 0 || strcmp(argv[0], "dir") == 0) {
      if(ftp_print_listing(client, argc >= 2 ? argv[1] : 0) < 0)
        dprintf(2, "6ftp: listing failed\n");
      continue;
    }

    if(strcmp(argv[0], "get") == 0) {
      if(argc < 2) {
        dprintf(2, "6ftp: get requires a remote path\n");
        continue;
      }
      if(ftp_get_file(client, argv[1], argc >= 3 ? argv[2] : 0) < 0)
        dprintf(2, "6ftp: download failed\n");
      continue;
    }

    if(strcmp(argv[0], "put") == 0) {
      if(argc < 2) {
        dprintf(2, "6ftp: put requires a local path\n");
        continue;
      }
      if(ftp_put_file(client, argv[1], argc >= 3 ? argv[2] : 0) < 0)
        dprintf(2, "6ftp: upload failed\n");
      continue;
    }

    if(strcmp(argv[0], "mkdir") == 0) {
      if(argc < 2) {
        dprintf(2, "6ftp: mkdir requires a remote path\n");
        continue;
      }
      ftp_command(client, reply, sizeof(reply), &code, "MKD %s", argv[1]);
      continue;
    }

    if(strcmp(argv[0], "rmdir") == 0) {
      if(argc < 2) {
        dprintf(2, "6ftp: rmdir requires a remote path\n");
        continue;
      }
      ftp_command(client, reply, sizeof(reply), &code, "RMD %s", argv[1]);
      continue;
    }

    if(strcmp(argv[0], "rm") == 0 || strcmp(argv[0], "delete") == 0) {
      if(argc < 2) {
        dprintf(2, "6ftp: rm requires a remote path\n");
        continue;
      }
      ftp_command(client, reply, sizeof(reply), &code, "DELE %s", argv[1]);
      continue;
    }

    if(strcmp(argv[0], "rename") == 0) {
      if(argc < 3) {
        dprintf(2, "6ftp: rename requires old and new names\n");
        continue;
      }
      if(ftp_command(client, reply, sizeof(reply), &code, "RNFR %s", argv[1]) < 0)
        continue;
      if(code / 100 != 3)
        continue;
      ftp_command(client, reply, sizeof(reply), &code, "RNTO %s", argv[2]);
      continue;
    }

    if(strcmp(argv[0], "binary") == 0) {
      if(ftp_set_type(client, 'I') < 0)
        dprintf(2, "6ftp: failed to set binary mode\n");
      continue;
    }

    if(strcmp(argv[0], "ascii") == 0) {
      if(ftp_set_type(client, 'A') < 0)
        dprintf(2, "6ftp: failed to set ASCII mode\n");
      continue;
    }

    if(strcmp(argv[0], "quote") == 0) {
      rest = skip_word(rest);
      if(*rest == 0) {
        dprintf(2, "6ftp: quote requires a command\n");
        continue;
      }
      ftp_command(client, reply, sizeof(reply), &code, "%s", rest);
      continue;
    }

    dprintf(2, "6ftp: unknown command: %s\n", argv[0]);
  }

  ftp_command(client, reply, sizeof(reply), &code, "QUIT");
  return 0;
}

int
main(int argc, char **argv)
{
  struct ftp_client client;
  char *user;
  char *pass;
  char *host;
  int port;
  int allow_prompt;
  int i;

  user = 0;
  pass = 0;
  host = 0;
  port = FTP_DEFAULT_PORT;

  i = 1;
  while(i < argc && argv[i][0] == '-') {
    if(strcmp(argv[i], "-u") == 0) {
      if(i + 1 >= argc)
        usage();
      user = argv[++i];
    } else if(strcmp(argv[i], "-p") == 0) {
      if(i + 1 >= argc)
        usage();
      pass = argv[++i];
    } else {
      usage();
    }
    i++;
  }

  if(i >= argc)
    usage();
  host = argv[i++];
  if(i < argc) {
    port = parse_port(argv[i++]);
    if(port < 0)
      usage();
  }
  if(i != argc)
    usage();

  memset(&client, 0, sizeof(client));
  client.ctrl_fd = -1;
  client.port = port;
  client.transfer_type = '?';
  snprintf(client.host, sizeof(client.host), "%s", host);
  client.host[sizeof(client.host) - 1] = 0;

  if(resolve_ipv4(host, &client.server_ip) < 0) {
    dprintf(2, "6ftp: cannot resolve host %s\n", host);
    exit(1);
  }

  client.ctrl_fd = open_tcp(client.server_ip, port);
  if(client.ctrl_fd < 0) {
    dprintf(2, "6ftp: connect failed to %s:%d\n", host, port);
    exit(1);
  }

  allow_prompt = isatty(0);
  if(ftp_login(&client, user, pass, allow_prompt) < 0) {
    close(client.ctrl_fd);
    exit(1);
  }

  run_shell(&client, allow_prompt && isatty(1));
  close(client.ctrl_fd);
  exit(0);
}
