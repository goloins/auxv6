#include "types.h"
#include "fcntl.h"
#include "auxv6/user.h"
#include "socket.h"

#define URL_HOST_MAX 127
#define URL_PATH_MAX 511
#define URL_OUT_MAX 255
#define RESP_BUF_MAX 1024
#define HDR_BUF_MAX 4096

struct http_url {
  char host[URL_HOST_MAX + 1];
  char path[URL_PATH_MAX + 1];
  int port;
};

static void
usage(void)
{
  dprintf(2, "usage: 6get [-o output] http://host[:port]/path\n");
  exit(1);
}

static int
is_digit(char c)
{
  return c >= '0' && c <= '9';
}

static int
parse_port(const char *s)
{
  int p;
  int i;

  if(s == 0 || s[0] == 0)
    return -1;

  p = 0;
  for(i = 0; s[i]; i++) {
    if(!is_digit(s[i]))
      return -1;
    p = p * 10 + (s[i] - '0');
    if(p > 65535)
      return -1;
  }

  if(p < 1)
    return -1;
  return p;
}

static int
parse_url(const char *url, struct http_url *out)
{
  const char *p;
  const char *slash;
  const char *host_end;
  const char *colon;
  int host_len;
  int path_len;
  int i;

  if(url == 0 || out == 0)
    return -1;

  if(strncmp(url, "http://", 7) != 0)
    return -1;

  p = url + 7;
  if(*p == 0)
    return -1;

  slash = strchr(p, '/');
  if(slash)
    host_end = slash;
  else
    host_end = p + strlen(p);

  if(host_end <= p)
    return -1;

  colon = 0;
  for(i = 0; p + i < host_end; i++) {
    if(p[i] == ':')
      colon = p + i;
  }

  out->port = 80;
  if(colon) {
    host_len = (int)(colon - p);
    if(host_len < 1 || host_len > URL_HOST_MAX)
      return -1;
    for(i = 0; i < host_len; i++)
      out->host[i] = p[i];
    out->host[host_len] = 0;

    out->port = parse_port(colon + 1);
    if(out->port < 0)
      return -1;
  } else {
    host_len = (int)(host_end - p);
    if(host_len < 1 || host_len > URL_HOST_MAX)
      return -1;
    for(i = 0; i < host_len; i++)
      out->host[i] = p[i];
    out->host[host_len] = 0;
  }

  if(slash == 0) {
    out->path[0] = '/';
    out->path[1] = 0;
    return 0;
  }

  path_len = (int)strlen(slash);
  if(path_len < 1 || path_len > URL_PATH_MAX)
    return -1;

  for(i = 0; i < path_len; i++)
    out->path[i] = slash[i];
  out->path[path_len] = 0;

  return 0;
}

static int
default_output_name(const char *path, char *out, int outsz)
{
  const char *base;
  const char *cut;
  int n;
  int i;

  if(path == 0 || out == 0 || outsz < 2)
    return -1;

  base = path;
  for(i = 0; path[i]; i++) {
    if(path[i] == '/')
      base = path + i + 1;
  }

  if(base[0] == 0) {
    if(outsz < 11)
      return -1;
    strcpy(out, "index.html");
    return 0;
  }

  cut = base;
  while(*cut && *cut != '?' && *cut != '#')
    cut++;

  n = (int)(cut - base);
  if(n <= 0) {
    if(outsz < 11)
      return -1;
    strcpy(out, "index.html");
    return 0;
  }

  if(n >= outsz)
    return -1;

  for(i = 0; i < n; i++)
    out[i] = base[i];
  out[n] = 0;
  return 0;
}

static int
send_all(int fd, const char *buf, int len)
{
  int off;
  int n;

  off = 0;
  while(off < len) {
    n = (int)send(fd, buf + off, len - off);
    if(n <= 0)
      return -1;
    off += n;
  }
  return 0;
}

static int
send_request(int fd, const struct http_url *u)
{
  if(send_all(fd, "GET ", 4) < 0)
    return -1;
  if(send_all(fd, u->path, (int)strlen(u->path)) < 0)
    return -1;
  if(send_all(fd, " HTTP/1.0\r\nHost: ", 17) < 0)
    return -1;
  if(send_all(fd, u->host, (int)strlen(u->host)) < 0)
    return -1;
  if(send_all(fd, "\r\nUser-Agent: 6get/1.0\r\nConnection: close\r\n\r\n", 49) < 0)
    return -1;
  return 0;
}

static int
header_end_index(const char *buf, int len)
{
  int i;

  for(i = 0; i + 3 < len; i++) {
    if(buf[i] == '\r' && buf[i + 1] == '\n' &&
       buf[i + 2] == '\r' && buf[i + 3] == '\n')
      return i;
  }

  return -1;
}

static int
parse_status_code(const char *hdr, int len)
{
  int i;
  int p;
  int code;

  if(len < 12)
    return -1;
  if(strncmp(hdr, "HTTP/", 5) != 0)
    return -1;

  p = -1;
  for(i = 0; i < len; i++) {
    if(hdr[i] == ' ') {
      p = i;
      break;
    }
    if(hdr[i] == '\r' || hdr[i] == '\n')
      break;
  }

  if(p < 0 || p + 3 >= len)
    return -1;
  if(!is_digit(hdr[p + 1]) || !is_digit(hdr[p + 2]) || !is_digit(hdr[p + 3]))
    return -1;

  code = (hdr[p + 1] - '0') * 100 + (hdr[p + 2] - '0') * 10 + (hdr[p + 3] - '0');
  return code;
}

static int
write_all(int fd, const char *buf, int len)
{
  int off;
  int n;

  off = 0;
  while(off < len) {
    n = (int)write(fd, buf + off, len - off);
    if(n <= 0)
      return -1;
    off += n;
  }
  return 0;
}

static int
fetch_to_file(const struct http_url *u, const char *outfile)
{
  int fd;
  int outfd;
  uint ip;
  struct sockaddr_in dst;
  int got_headers;
  int status;
  int total;
  int n;
  int hdrlen;
  int body_off;
  char hdr[HDR_BUF_MAX];
  char buf[RESP_BUF_MAX];

  if(resolve_ipv4(u->host, &ip) < 0) {
    dprintf(2, "6get: cannot resolve host %s\n", u->host);
    return -1;
  }

  fd = socket(AF_INET, SOCK_STREAM, 0);
  if(fd < 0) {
    dprintf(2, "6get: socket failed\n");
    return -1;
  }

  memset(&dst, 0, sizeof(dst));
  dst.sin_family = AF_INET;
  dst.sin_port = (ushort)u->port;
  dst.sin_addr = ip;

  if(connect(fd, &dst, sizeof(dst)) < 0) {
    dprintf(2, "6get: connect failed\n");
    close(fd);
    return -1;
  }

  if(send_request(fd, u) < 0) {
    dprintf(2, "6get: send request failed\n");
    close(fd);
    return -1;
  }

  got_headers = 0;
  status = -1;
  total = 0;
  hdrlen = 0;
  outfd = -1;

  while((n = (int)recv(fd, buf, sizeof(buf))) > 0) {
    if(!got_headers) {
      int i;
      int end;

      if(hdrlen + n > HDR_BUF_MAX) {
        dprintf(2, "6get: response headers too large\n");
        close(fd);
        return -1;
      }

      for(i = 0; i < n; i++)
        hdr[hdrlen + i] = buf[i];
      hdrlen += n;

      end = header_end_index(hdr, hdrlen);
      if(end < 0)
        continue;

      status = parse_status_code(hdr, end);
      if(status < 200 || status >= 300) {
        dprintf(2, "6get: HTTP status %d\n", status);
        close(fd);
        return -1;
      }

      outfd = open(outfile, O_CREATE | O_WRONLY | O_TRUNC);
      if(outfd < 0) {
        dprintf(2, "6get: cannot open output %s\n", outfile);
        close(fd);
        return -1;
      }

      got_headers = 1;
      body_off = end + 4;
      if(body_off < hdrlen) {
        if(write_all(outfd, hdr + body_off, hdrlen - body_off) < 0) {
          dprintf(2, "6get: write failed\n");
          close(outfd);
          close(fd);
          unlink(outfile);
          return -1;
        }
        total += hdrlen - body_off;
      }
      continue;
    }

    if(write_all(outfd, buf, n) < 0) {
      dprintf(2, "6get: write failed\n");
      close(outfd);
      close(fd);
      unlink(outfile);
      return -1;
    }
    total += n;
  }

  if(n < 0) {
    dprintf(2, "6get: recv failed\n");
    if(outfd >= 0) {
      close(outfd);
      unlink(outfile);
    }
    close(fd);
    return -1;
  }

  if(!got_headers) {
    dprintf(2, "6get: invalid HTTP response\n");
    close(fd);
    return -1;
  }

  close(fd);
  close(outfd);
  dprintf(1, "6get: saved %s (%d bytes)\n", outfile, total);
  return 0;
}

int
main(int argc, char **argv)
{
  int i;
  const char *url;
  const char *outfile_opt;
  char outname[URL_OUT_MAX + 1];
  struct http_url u;

  url = 0;
  outfile_opt = 0;

  i = 1;
  while(i < argc) {
    if(strcmp(argv[i], "-o") == 0) {
      if(i + 1 >= argc)
        usage();
      outfile_opt = argv[i + 1];
      i += 2;
      continue;
    }

    if(url)
      usage();
    url = argv[i];
    i++;
  }

  if(url == 0)
    usage();

  if(parse_url(url, &u) < 0) {
    dprintf(2, "6get: invalid URL (expected http://host[:port]/path)\n");
    exit(1);
  }

  if(outfile_opt) {
    if((int)strlen(outfile_opt) > URL_OUT_MAX || outfile_opt[0] == 0) {
      dprintf(2, "6get: invalid output path\n");
      exit(1);
    }
    strcpy(outname, outfile_opt);
  } else {
    if(default_output_name(u.path, outname, sizeof(outname)) < 0) {
      dprintf(2, "6get: cannot derive output filename\n");
      exit(1);
    }
  }

  if(fetch_to_file(&u, outname) < 0)
    exit(1);

  exit(0);
}
