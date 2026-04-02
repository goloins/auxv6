#include "types.h"
#include "fcntl.h"
#include "stdio.h"
#include "auxv6/user.h"
#include "socket.h"

#define URL_HOST_MAX 127
#define URL_PATH_MAX 511
#define URL_OUT_MAX 255
#define RESP_BUF_MAX 1024
#define HDR_BUF_MAX 4096
#define REDIRECT_MAX 4
#define LOCATION_MAX 511
#define SIXGET_VERSION "6get 0.3"
#define RECV_TIMEOUT_TICKS 100
#define POST_HEADER_IDLE_LIMIT 5

static int g_debug = 0;
static int g_quiet = 0;
static char g_hdr_buf[HDR_BUF_MAX];
static char g_resp_buf[RESP_BUF_MAX];

#define PROGRESS(...) do { if(!g_quiet) printf(__VA_ARGS__); } while(0)
#define ERROR(...) do { printf(__VA_ARGS__); } while(0)
#define DEBUG(...) do { if(g_debug) printf(__VA_ARGS__); } while(0)

struct http_url;
static int is_lws(char c);
static int starts_with_nocase(const char *s, const char *pfx, int n);
static int parse_url(const char *url, struct http_url *out);

struct http_url {
  char host[URL_HOST_MAX + 1];
  char path[URL_PATH_MAX + 1];
  int port;
};

static void
usage(void)
{
  ERROR("usage: 6get [-d] [-q] [-o output] http://host[:port]/path\n");
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

  if(strncmp(url, "https://", 8) == 0) {
    ERROR("6get: https is not supported yet (TLS not implemented)\n");
    return -1;
  }

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
parse_content_length(const char *hdr, int len)
{
  int i;

  i = 0;
  while(i < len) {
    int line_start;
    int line_end;
    int j;
    int v;

    line_start = i;
    while(i < len && hdr[i] != '\n' && hdr[i] != '\r')
      i++;
    line_end = i;
    while(i < len && (hdr[i] == '\n' || hdr[i] == '\r'))
      i++;

    if(line_end <= line_start)
      continue;
    if(!starts_with_nocase(hdr + line_start, "content-length:", line_end - line_start))
      continue;

    j = line_start + 15;
    while(j < line_end && is_lws(hdr[j]))
      j++;
    if(j >= line_end)
      return -1;

    v = 0;
    while(j < line_end && hdr[j] >= '0' && hdr[j] <= '9') {
      if(v > 214748364)
        return -1;
      v = v * 10 + (hdr[j] - '0');
      j++;
    }
    return v;
  }

  return -1;
}

static int
is_lws(char c)
{
  return c == ' ' || c == '\t';
}

static char
to_lower_ascii(char c)
{
  if(c >= 'A' && c <= 'Z')
    return c + ('a' - 'A');
  return c;
}

static int
starts_with_nocase(const char *s, const char *pfx, int n)
{
  int i;

  for(i = 0; i < n; i++) {
    if(pfx[i] == 0)
      return 1;
    if(s[i] == 0)
      return 0;
    if(to_lower_ascii(s[i]) != to_lower_ascii(pfx[i]))
      return 0;
  }

  return pfx[i] == 0;
}

static int
find_location_header(const char *hdr, int len, char *out, int outsz)
{
  int i;

  i = 0;
  while(i < len) {
    int line_start;
    int line_end;
    int j;
    int vstart;
    int vend;
    int n;

    line_start = i;
    while(i < len && hdr[i] != '\n' && hdr[i] != '\r')
      i++;
    line_end = i;

    while(i < len && (hdr[i] == '\n' || hdr[i] == '\r'))
      i++;

    if(line_end <= line_start)
      continue;

    if(!starts_with_nocase(hdr + line_start, "location:", line_end - line_start))
      continue;

    j = line_start + 9;
    while(j < line_end && is_lws(hdr[j]))
      j++;
    vstart = j;

    vend = line_end;
    while(vend > vstart && is_lws(hdr[vend - 1]))
      vend--;

    n = vend - vstart;
    if(n <= 0)
      return -1;
    if(n >= outsz)
      n = outsz - 1;

    for(j = 0; j < n; j++)
      out[j] = hdr[vstart + j];
    out[n] = 0;
    return 0;
  }

  return -1;
}

static int
resolve_redirect_url(const struct http_url *base, const char *loc, struct http_url *out)
{
  int i;

  if(strncmp(loc, "http://", 7) == 0)
    return parse_url(loc, out);

  if(strncmp(loc, "https://", 8) == 0) {
    ERROR("6get: redirect target requires https (unsupported): %s\n", loc);
    return -1;
  }

  *out = *base;

  if(loc[0] == '/') {
    if((int)strlen(loc) > URL_PATH_MAX)
      return -1;
    strcpy(out->path, loc);
    return 0;
  }

  i = 0;
  while(base->path[i] && i < URL_PATH_MAX)
    i++;

  while(i > 0 && base->path[i - 1] != '/')
    i--;

  if(i == 0) {
    out->path[0] = '/';
    i = 1;
  }

  while(*loc) {
    if(i >= URL_PATH_MAX)
      return -1;
    out->path[i++] = *loc++;
  }
  out->path[i] = 0;
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
  char hostline[URL_HOST_MAX + 32];

  if(send_all(fd, "GET ", 4) < 0)
    return -1;
  if(send_all(fd, u->path, (int)strlen(u->path)) < 0)
    return -1;
  if(send_all(fd, " HTTP/1.0\r\n", 11) < 0)
    return -1;

  if(u->port == 80)
    snprintf(hostline, sizeof(hostline), "Host: %s\r\n", u->host);
  else
    snprintf(hostline, sizeof(hostline), "Host: %s:%d\r\n", u->host, u->port);

  if(send_all(fd, hostline, (int)strlen(hostline)) < 0)
    return -1;
  if(send_all(fd, "User-Agent: 6get/1.0\r\nConnection: close\r\n\r\n",
              (int)strlen("User-Agent: 6get/1.0\r\nConnection: close\r\n\r\n")) < 0)
    return -1;

  DEBUG("6get[debug]: request sent for %s:%d%s\n", u->host, u->port, u->path);
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

  for(i = 0; i + 1 < len; i++) {
    if(buf[i] == '\n' && buf[i + 1] == '\n')
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

static void
progress_update(int total, int content_len, int *last_pct, int done)
{
  int pct;
  int filled;
  int i;
  char bar[21];

  if(g_quiet || content_len <= 0 || last_pct == 0)
    return;

  pct = (total * 100) / content_len;
  if(pct > 100)
    pct = 100;

  if(!done && pct == *last_pct)
    return;
  *last_pct = pct;

  filled = (pct * 20) / 100;
  for(i = 0; i < 20; i++)
    bar[i] = (i < filled) ? '=' : ' ';
  bar[20] = 0;

  printf("\r6get: [%s] %3d%% (%d/%d)", bar, pct, total, content_len);
  if(done)
    printf("\n");
}

static int __attribute__((noinline))
fetch_to_file(const struct http_url *u, const char *outfile, char *redirect, int redirsz)
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
  int content_len;
  int idle_count;
  int last_pct;

  if(redirect && redirsz > 0)
    redirect[0] = 0;

  PROGRESS("6get: resolving %s\n", u->host);
  if(resolve_ipv4(u->host, &ip) < 0) {
    ERROR("6get: cannot resolve host %s\n", u->host);
    return -1;
  }
  DEBUG("6get[debug]: resolved host %s to 0x%x\n", u->host, ip);

  fd = socket(AF_INET, SOCK_STREAM, 0);
  if(fd < 0) {
    ERROR("6get: socket failed\n");
    return -1;
  }

  memset(&dst, 0, sizeof(dst));
  dst.sin_family = AF_INET;
  dst.sin_port = (ushort)u->port;
  dst.sin_addr = ip;

  PROGRESS("6get: connecting to %s:%d\n", u->host, u->port);
  if(connect(fd, &dst, sizeof(dst)) < 0) {
    ERROR("6get: connect failed\n");
    close(fd);
    return -1;
  }
  PROGRESS("6get: connected\n");

  if(send_request(fd, u) < 0) {
    ERROR("6get: send request failed\n");
    close(fd);
    return -1;
  }

  got_headers = 0;
  status = -1;
  total = 0;
  hdrlen = 0;
  content_len = -1;
  idle_count = 0;
  last_pct = -1;
  outfd = -1;

  while(1) {
    n = (int)recvtimeout(fd, g_resp_buf, sizeof(g_resp_buf), RECV_TIMEOUT_TICKS);
    if(n == 0) {
      if(!got_headers)
        continue;
      idle_count++;
      if(content_len >= 0 && total >= content_len)
        break;
      if(idle_count >= POST_HEADER_IDLE_LIMIT) {
        DEBUG("6get[debug]: idle timeout after headers, ending transfer\n");
        break;
      }
      continue;
    }

    if(n < 0)
      break;
    idle_count = 0;

    if(!got_headers) {
      int i;
      int end;

      if(hdrlen + n > HDR_BUF_MAX) {
        ERROR("6get: response headers too large\n");
        close(fd);
        return -1;
      }

      for(i = 0; i < n; i++)
        g_hdr_buf[hdrlen + i] = g_resp_buf[i];
      hdrlen += n;

      end = header_end_index(g_hdr_buf, hdrlen);
      if(end < 0)
        continue;

      status = parse_status_code(g_hdr_buf, end);
      PROGRESS("6get: HTTP status %d\n", status);
      if(status < 200 || status >= 300) {
        ERROR("6get: HTTP status %d\n", status);
        if(status >= 300 && status < 400 && redirect && redirsz > 0) {
          if(find_location_header(g_hdr_buf, end, redirect, redirsz) == 0) {
            PROGRESS("6get: redirect location: %s\n", redirect);
            close(fd);
            return 1;
          }
          ERROR("6get: redirect response without Location header\n");
        }
        close(fd);
        return -1;
      }

      content_len = parse_content_length(g_hdr_buf, end);
      if(content_len >= 0) {
        DEBUG("6get[debug]: content-length=%d\n", content_len);
        progress_update(total, content_len, &last_pct, 0);
      }

      outfd = open(outfile, O_CREAT | O_WRONLY | O_TRUNC);
      if(outfd < 0) {
        ERROR("6get: cannot open output %s\n", outfile);
        close(fd);
        return -1;
      }

      got_headers = 1;
      body_off = end + 4;
      if(end + 1 < hdrlen && g_hdr_buf[end] == '\n' && g_hdr_buf[end + 1] == '\n')
        body_off = end + 2;

      DEBUG("6get[debug]: header bytes=%d body starts at=%d\n", hdrlen, body_off);
      if(body_off < hdrlen) {
        if(write_all(outfd, g_hdr_buf + body_off, hdrlen - body_off) < 0) {
          ERROR("6get: write failed\n");
          close(outfd);
          close(fd);
          unlink(outfile);
          return -1;
        }
        total += hdrlen - body_off;
        progress_update(total, content_len, &last_pct, 0);
        if(content_len >= 0 && total >= content_len)
          break;
      }
      continue;
    }

    if(write_all(outfd, g_resp_buf, n) < 0) {
      ERROR("6get: write failed\n");
      close(outfd);
      close(fd);
      unlink(outfile);
      return -1;
    }
    total += n;
    progress_update(total, content_len, &last_pct, 0);

    if(content_len >= 0 && total >= content_len)
      break;
  }

  if(n < 0) {
    ERROR("6get: recv failed\n");
    if(outfd >= 0) {
      close(outfd);
      unlink(outfile);
    }
    close(fd);
    return -1;
  }

  if(!got_headers) {
    ERROR("6get: invalid HTTP response\n");
    close(fd);
    return -1;
  }

  progress_update(total, content_len, &last_pct, 1);

  close(fd);
  close(outfd);
  PROGRESS("6get: saved %s (%d bytes)\n", outfile, total);
  return 0;
}

int
main(int argc, char **argv)
{
  int i;
  int redirects;
  int rc;
  const char *url;
  const char *outfile_opt;
  char outname[URL_OUT_MAX + 1];
  char redirect[LOCATION_MAX + 1];
  struct http_url u;
  struct http_url cur;
  struct http_url next;

  url = 0;
  outfile_opt = 0;

  i = 1;
  while(i < argc) {
    if(strcmp(argv[i], "-h") == 0 || strcmp(argv[i], "--help") == 0)
      usage();

    if(strcmp(argv[i], "-V") == 0 || strcmp(argv[i], "--version") == 0) {
      printf("%s\n", SIXGET_VERSION);
      exit(0);
    }

    if(strcmp(argv[i], "-d") == 0) {
      g_debug = 1;
      i++;
      continue;
    }

    if(strcmp(argv[i], "-q") == 0) {
      g_quiet = 1;
      i++;
      continue;
    }

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

  if(g_debug)
    PROGRESS("6get: debug logging enabled\n");

  if(parse_url(url, &u) < 0) {
    ERROR("6get: invalid URL (expected http://host[:port]/path)\n");
    exit(1);
  }

  if(outfile_opt) {
    if((int)strlen(outfile_opt) > URL_OUT_MAX || outfile_opt[0] == 0) {
      ERROR("6get: invalid output path\n");
      exit(1);
    }
    strcpy(outname, outfile_opt);
  } else {
    if(default_output_name(u.path, outname, sizeof(outname)) < 0) {
      ERROR("6get: cannot derive output filename\n");
      exit(1);
    }
  }

  cur = u;
  redirects = 0;
  while(1) {
    DEBUG("6get[debug]: redirect iteration %d\n", redirects);
    rc = fetch_to_file(&cur, outname, redirect, sizeof(redirect));
    if(rc == 0)
      break;
    if(rc < 0)
      exit(1);

    redirects++;
    if(redirects > REDIRECT_MAX) {
      ERROR("6get: too many redirects\n");
      exit(1);
    }

    if(resolve_redirect_url(&cur, redirect, &next) < 0)
      exit(1);

    PROGRESS("6get: following redirect to http://%s:%d%s\n", next.host, next.port, next.path);
    cur = next;
  }

  exit(0);
}
