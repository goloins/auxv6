#include "types.h"
#include "fcntl.h"
#include "stdio.h"
#include "auxv6/user.h"
#include "socket.h"
#include "libterm.h"

#define ABROWSE_VERSION "abrowse 0.1"

#define URL_HOST_MAX 127
#define URL_PATH_MAX 511
#define URL_STR_MAX 767

#define RESP_BUF_MAX 1024
#define HDR_BUF_MAX 4096
#define BODY_BUF_MAX 65536
#define TEXT_BUF_MAX 98304
#define REDIRECT_MAX 4
#define LOCATION_MAX 511

#define RECV_TIMEOUT_TICKS 100
#define POST_HEADER_IDLE_LIMIT 5
#define POST_HEADER_STALL_LIMIT 60

#define MAX_LINKS 128
#define LINK_LABEL_MAX 63
#define LINK_URL_MAX 255

#define MAX_LINES 4096

struct http_url {
  char host[URL_HOST_MAX + 1];
  char path[URL_PATH_MAX + 1];
  int port;
};

struct page_link {
  char label[LINK_LABEL_MAX + 1];
  char url[LINK_URL_MAX + 1];
};

struct page_model {
  char page_url[URL_STR_MAX + 1];
  char content_type[64];
  char text[TEXT_BUF_MAX];
  int text_len;
  struct page_link links[MAX_LINKS];
  int nlinks;
};

static char g_hdr_buf[HDR_BUF_MAX];
static char g_resp_buf[RESP_BUF_MAX];
static char g_body_buf[BODY_BUF_MAX];

static struct termstate g_ts;
static struct page_model g_page;

static int g_line_start[MAX_LINES];
static int g_line_len[MAX_LINES];
static int g_line_count;
static int g_layout_cols;
static int g_scroll;
static int g_selected_link;
static char g_status[160];

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

static char
to_lower_ascii(char c)
{
  if(c >= 'A' && c <= 'Z')
    return c + ('a' - 'A');
  return c;
}

static int
streq_nocase_n(const char *a, const char *b, int n)
{
  int i;

  for(i = 0; i < n; i++) {
    if(b[i] == 0)
      return a[i] == 0;
    if(a[i] == 0)
      return 0;
    if(to_lower_ascii(a[i]) != to_lower_ascii(b[i]))
      return 0;
  }
  return b[n] == 0;
}

static int
starts_with_nocase(const char *s, const char *pfx)
{
  int i;
  for(i = 0; pfx[i]; i++) {
    if(s[i] == 0)
      return 0;
    if(to_lower_ascii(s[i]) != to_lower_ascii(pfx[i]))
      return 0;
  }
  return 1;
}

static int
contains_nocase(const char *s, const char *needle)
{
  int i;
  int nl;

  nl = strlen(needle);
  if(nl == 0)
    return 1;

  for(i = 0; s[i]; i++) {
    int j;
    for(j = 0; j < nl; j++) {
      if(s[i + j] == 0)
        return 0;
      if(to_lower_ascii(s[i + j]) != to_lower_ascii(needle[j]))
        break;
    }
    if(j == nl)
      return 1;
  }
  return 0;
}

static void
status_set(const char *msg)
{
  snprintf(g_status, sizeof(g_status), "%s", msg);
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

  if(strncmp(url, "https://", 8) == 0)
    return -2;

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

static void
url_to_string(const struct http_url *u, char *out, int outsz)
{
  if(u->port == 80)
    snprintf(out, outsz, "http://%s%s", u->host, u->path);
  else
    snprintf(out, outsz, "http://%s:%d%s", u->host, u->port, u->path);
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
  if(send_all(fd, "User-Agent: abrowse/0.1\r\nConnection: close\r\n\r\n",
              (int)strlen("User-Agent: abrowse/0.1\r\nConnection: close\r\n\r\n")) < 0)
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
    if(!starts_with_nocase(hdr + line_start, "content-length:"))
      continue;

    j = line_start + 15;
    while(j < line_end && (hdr[j] == ' ' || hdr[j] == '\t'))
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
copy_header_value(const char *hdr, int len, const char *key, char *out, int outsz)
{
  int i;
  int klen;

  klen = strlen(key);
  i = 0;
  while(i < len) {
    int line_start;
    int line_end;
    int j;
    int n;

    line_start = i;
    while(i < len && hdr[i] != '\n' && hdr[i] != '\r')
      i++;
    line_end = i;
    while(i < len && (hdr[i] == '\n' || hdr[i] == '\r'))
      i++;

    if(line_end <= line_start)
      continue;
    if(line_end - line_start < klen)
      continue;
    if(!streq_nocase_n(hdr + line_start, key, klen))
      continue;

    j = line_start + klen;
    while(j < line_end && (hdr[j] == ' ' || hdr[j] == '\t'))
      j++;

    n = line_end - j;
    while(n > 0 && (hdr[j + n - 1] == ' ' || hdr[j + n - 1] == '\t'))
      n--;
    if(n <= 0)
      return -1;
    if(n >= outsz)
      n = outsz - 1;

    memmove(out, hdr + j, n);
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

  if(strncmp(loc, "https://", 8) == 0)
    return -2;

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
fetch_once(const struct http_url *u, char *body, int body_max, int *body_len,
           char *ctype, int ctype_sz, char *redirect, int redirsz, int *status_out)
{
  int fd;
  uint ip;
  struct sockaddr_in dst;
  int got_headers;
  int status;
  int n;
  int hdrlen;
  int body_off;
  int content_len;
  int idle_count;
  int total;

  if(body_len)
    *body_len = 0;
  if(ctype && ctype_sz > 0)
    ctype[0] = 0;
  if(redirect && redirsz > 0)
    redirect[0] = 0;
  if(status_out)
    *status_out = -1;

  if(resolve_ipv4(u->host, &ip) < 0)
    return -1;

  fd = socket(AF_INET, SOCK_STREAM, 0);
  if(fd < 0)
    return -1;

  memset(&dst, 0, sizeof(dst));
  dst.sin_family = AF_INET;
  dst.sin_port = (ushort)u->port;
  dst.sin_addr = ip;

  if(connect(fd, &dst, sizeof(dst)) < 0) {
    close(fd);
    return -1;
  }

  if(send_request(fd, u) < 0) {
    close(fd);
    return -1;
  }

  got_headers = 0;
  status = -1;
  hdrlen = 0;
  content_len = -1;
  idle_count = 0;
  total = 0;

  while(1) {
    n = (int)recvtimeout(fd, g_resp_buf, sizeof(g_resp_buf), RECV_TIMEOUT_TICKS);
    if(n == RECV_TIMEOUT_EXPIRED) {
      if(!got_headers)
        continue;
      idle_count++;
      if(content_len >= 0) {
        if(idle_count >= POST_HEADER_STALL_LIMIT) {
          close(fd);
          return -1;
        }
        continue;
      }
      if(idle_count >= POST_HEADER_IDLE_LIMIT)
        break;
      continue;
    }

    if(n == 0) {
      if(content_len >= 0 && total < content_len) {
        close(fd);
        return -1;
      }
      break;
    }

    if(n < 0)
      break;
    idle_count = 0;

    if(!got_headers) {
      int i;
      int end;

      if(hdrlen + n > HDR_BUF_MAX) {
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
      if(status_out)
        *status_out = status;

      if(status >= 300 && status < 400 && redirect && redirsz > 0) {
        if(copy_header_value(g_hdr_buf, end, "Location:", redirect, redirsz) == 0) {
          close(fd);
          return 1;
        }
      }

      if(status < 200 || status >= 300) {
        close(fd);
        return -1;
      }

      content_len = parse_content_length(g_hdr_buf, end);
      copy_header_value(g_hdr_buf, end, "Content-Type:", ctype, ctype_sz);

      got_headers = 1;
      body_off = end + 4;
      if(end + 1 < hdrlen && g_hdr_buf[end] == '\n' && g_hdr_buf[end + 1] == '\n')
        body_off = end + 2;

      if(body_off < hdrlen) {
        int chunk;
        chunk = hdrlen - body_off;
        if(total + chunk >= body_max)
          chunk = body_max - 1 - total;
        if(chunk < 0)
          chunk = 0;
        if(chunk > 0)
          memmove(body + total, g_hdr_buf + body_off, chunk);
        total += chunk;
        if(total >= body_max - 1) {
          close(fd);
          return -3;
        }
        if(content_len >= 0 && total >= content_len)
          break;
      }
      continue;
    }

    if(total + n >= body_max)
      n = body_max - 1 - total;
    if(n < 0)
      n = 0;
    if(n > 0)
      memmove(body + total, g_resp_buf, n);
    total += n;
    if(total >= body_max - 1) {
      close(fd);
      return -3;
    }

    if(content_len >= 0 && total >= content_len)
      break;
  }

  close(fd);
  if(n < 0)
    return -1;
  if(!got_headers)
    return -1;

  body[total] = 0;
  if(body_len)
    *body_len = total;
  return 0;
}

static int
fetch_url(const char *url, struct page_model *page, char *errmsg, int errmsg_sz)
{
  struct http_url cur;
  struct http_url next;
  int redirects;
  int rc;
  char redirect[LOCATION_MAX + 1];
  int status;
  int body_len;

  rc = parse_url(url, &cur);
  if(rc == -2) {
    snprintf(errmsg, errmsg_sz, "https is not supported yet");
    return -1;
  }
  if(rc < 0) {
    if(strncmp(url, "gemini://", 9) == 0)
      snprintf(errmsg, errmsg_sz, "gemini:// requires TLS; not supported yet");
    else
      snprintf(errmsg, errmsg_sz, "invalid URL, expected http://host/path");
    return -1;
  }

  redirects = 0;
  while(1) {
    rc = fetch_once(&cur, g_body_buf, sizeof(g_body_buf), &body_len,
                    page->content_type, sizeof(page->content_type),
                    redirect, sizeof(redirect), &status);
    if(rc == 0)
      break;

    if(rc == 1) {
      redirects++;
      if(redirects > REDIRECT_MAX) {
        snprintf(errmsg, errmsg_sz, "too many redirects");
        return -1;
      }
      rc = resolve_redirect_url(&cur, redirect, &next);
      if(rc == -2) {
        snprintf(errmsg, errmsg_sz, "redirect requires https (unsupported)");
        return -1;
      }
      if(rc < 0) {
        snprintf(errmsg, errmsg_sz, "invalid redirect target");
        return -1;
      }
      cur = next;
      continue;
    }

    if(rc == -3)
      snprintf(errmsg, errmsg_sz, "response too large (max %d bytes)", BODY_BUF_MAX - 1);
    else if(status > 0)
      snprintf(errmsg, errmsg_sz, "HTTP status %d", status);
    else
      snprintf(errmsg, errmsg_sz, "transfer failed");
    return -1;
  }

  url_to_string(&cur, page->page_url, sizeof(page->page_url));
  return 0;
}

static void
page_reset(struct page_model *p)
{
  p->text[0] = 0;
  p->text_len = 0;
  p->nlinks = 0;
}

static void
text_append_char(struct page_model *p, char c)
{
  if(p->text_len >= TEXT_BUF_MAX - 1)
    return;
  p->text[p->text_len++] = c;
  p->text[p->text_len] = 0;
}

static void
text_append_str(struct page_model *p, const char *s)
{
  while(*s)
    text_append_char(p, *s++);
}

static void
text_ensure_blank_line(struct page_model *p)
{
  if(p->text_len == 0) {
    text_append_char(p, '\n');
    return;
  }
  if(p->text_len >= 2 && p->text[p->text_len - 1] == '\n' && p->text[p->text_len - 2] == '\n')
    return;
  if(p->text[p->text_len - 1] != '\n')
    text_append_char(p, '\n');
  text_append_char(p, '\n');
}

static void
trim_copy(char *dst, int dstsz, const char *src)
{
  int i;
  int j;
  int n;

  n = strlen(src);
  i = 0;
  while(i < n && is_space(src[i]))
    i++;
  j = n;
  while(j > i && is_space(src[j - 1]))
    j--;

  if(j - i >= dstsz)
    j = i + dstsz - 1;

  n = 0;
  while(i < j)
    dst[n++] = src[i++];
  dst[n] = 0;
}

static void
link_add(struct page_model *p, const char *label, const char *url)
{
  int idx;
  char tlabel[LINK_LABEL_MAX + 1];
  char turl[LINK_URL_MAX + 1];

  if(p->nlinks >= MAX_LINKS)
    return;

  trim_copy(tlabel, sizeof(tlabel), label ? label : "");
  trim_copy(turl, sizeof(turl), url ? url : "");
  if(turl[0] == 0)
    return;

  idx = p->nlinks;
  snprintf(p->links[idx].label, sizeof(p->links[idx].label), "%s", tlabel[0] ? tlabel : turl);
  snprintf(p->links[idx].url, sizeof(p->links[idx].url), "%s", turl);
  p->nlinks++;

  text_append_char(p, '[');
  if(idx + 1 >= 100) {
    text_append_char(p, (char)('0' + ((idx + 1) / 100) % 10));
  }
  if(idx + 1 >= 10) {
    text_append_char(p, (char)('0' + ((idx + 1) / 10) % 10));
  }
  text_append_char(p, (char)('0' + ((idx + 1) % 10)));
  text_append_char(p, ']');
}

static void
md_render_inline(struct page_model *p, const char *line)
{
  int i;

  for(i = 0; line[i]; ) {
    int j;
    int k;

    if(line[i] == '[') {
      j = i + 1;
      while(line[j] && line[j] != ']')
        j++;
      if(line[j] == ']' && line[j + 1] == '(') {
        k = j + 2;
        while(line[k] && line[k] != ')')
          k++;
        if(line[k] == ')' && j > i + 1 && k > j + 2) {
          char label[LINK_LABEL_MAX + 1];
          char url[LINK_URL_MAX + 1];
          int a;
          int ln;
          int un;

          ln = j - (i + 1);
          if(ln > LINK_LABEL_MAX)
            ln = LINK_LABEL_MAX;
          for(a = 0; a < ln; a++)
            label[a] = line[i + 1 + a];
          label[ln] = 0;

          un = k - (j + 2);
          if(un > LINK_URL_MAX)
            un = LINK_URL_MAX;
          for(a = 0; a < un; a++)
            url[a] = line[j + 2 + a];
          url[un] = 0;

          text_append_str(p, label);
          link_add(p, label, url);
          i = k + 1;
          continue;
        }
      }
    }

    if(line[i] == '`' || line[i] == '*' || line[i] == '_') {
      i++;
      continue;
    }

    text_append_char(p, line[i]);
    i++;
  }
}

static void
render_markdown(struct page_model *p, const char *body)
{
  int i;
  char line[512];
  int llen;
  int in_code;

  page_reset(p);
  llen = 0;
  in_code = 0;

  for(i = 0; ; i++) {
    char c = body[i];
    if(c == 0 || c == '\n' || llen >= (int)sizeof(line) - 1) {
      int pos;
      line[llen] = 0;
      pos = 0;
      while(line[pos] == ' ' || line[pos] == '\t')
        pos++;

      if(strncmp(line + pos, "```", 3) == 0) {
        in_code = !in_code;
        text_append_char(p, '\n');
      } else if(in_code) {
        text_append_str(p, "    ");
        text_append_str(p, line + pos);
        text_append_char(p, '\n');
      } else if(line[pos] == 0) {
        text_append_char(p, '\n');
      } else if(strncmp(line + pos, "# ", 2) == 0) {
        text_ensure_blank_line(p);
        md_render_inline(p, line + pos + 2);
        text_append_char(p, '\n');
        text_append_char(p, '\n');
      } else if(strncmp(line + pos, "## ", 3) == 0 || strncmp(line + pos, "### ", 4) == 0) {
        text_ensure_blank_line(p);
        if(strncmp(line + pos, "## ", 3) == 0)
          md_render_inline(p, line + pos + 3);
        else
          md_render_inline(p, line + pos + 4);
        text_append_char(p, '\n');
      } else if(strncmp(line + pos, "- ", 2) == 0) {
        text_append_str(p, " * ");
        md_render_inline(p, line + pos + 2);
        text_append_char(p, '\n');
      } else {
        md_render_inline(p, line + pos);
        text_append_char(p, '\n');
      }

      llen = 0;
      if(c == 0)
        break;
      continue;
    }

    if(c != '\r')
      line[llen++] = c;
  }
}

static int
html_tag_name(const char *s, int n, char *out, int outsz, int *is_close)
{
  int i;
  int j;

  *is_close = 0;
  i = 0;
  while(i < n && is_space(s[i]))
    i++;
  if(i < n && s[i] == '/') {
    *is_close = 1;
    i++;
  }
  while(i < n && is_space(s[i]))
    i++;

  j = 0;
  while(i < n && j < outsz - 1) {
    char c;
    c = s[i];
    if(is_space(c) || c == '/' || c == '>')
      break;
    out[j++] = to_lower_ascii(c);
    i++;
  }
  out[j] = 0;
  return i;
}

static int
html_find_href(const char *s, int n, char *out, int outsz)
{
  int i;

  for(i = 0; i + 5 < n; i++) {
    if(to_lower_ascii(s[i]) == 'h' &&
       to_lower_ascii(s[i + 1]) == 'r' &&
       to_lower_ascii(s[i + 2]) == 'e' &&
       to_lower_ascii(s[i + 3]) == 'f') {
      int j;
      int q;
      int k;

      j = i + 4;
      while(j < n && is_space(s[j]))
        j++;
      if(j >= n || s[j] != '=')
        continue;
      j++;
      while(j < n && is_space(s[j]))
        j++;
      if(j >= n)
        continue;

      if(s[j] == '\'' || s[j] == '"') {
        q = s[j++];
        k = 0;
        while(j < n && s[j] != q && k < outsz - 1)
          out[k++] = s[j++];
        out[k] = 0;
        return k > 0 ? 0 : -1;
      }

      k = 0;
      while(j < n && !is_space(s[j]) && s[j] != '>' && k < outsz - 1)
        out[k++] = s[j++];
      out[k] = 0;
      return k > 0 ? 0 : -1;
    }
  }

  return -1;
}

static void
html_emit_entity(struct page_model *p, const char *s, int n)
{
  if(n == 3 && strncmp(s, "amp", 3) == 0) {
    text_append_char(p, '&');
    return;
  }
  if(n == 2 && strncmp(s, "lt", 2) == 0) {
    text_append_char(p, '<');
    return;
  }
  if(n == 2 && strncmp(s, "gt", 2) == 0) {
    text_append_char(p, '>');
    return;
  }
  if(n == 4 && strncmp(s, "quot", 4) == 0) {
    text_append_char(p, '"');
    return;
  }
  if(n == 3 && strncmp(s, "#39", 3) == 0) {
    text_append_char(p, '\'');
    return;
  }
  text_append_char(p, '&');
  while(n-- > 0)
    text_append_char(p, *s++);
  text_append_char(p, ';');
}

static void
render_html(struct page_model *p, const char *body)
{
  int i;
  int in_tag;
  char tagbuf[512];
  int tlen;
  int in_anchor;
  int anchor_text_start;
  char anchor_href[LINK_URL_MAX + 1];

  page_reset(p);
  in_tag = 0;
  tlen = 0;
  in_anchor = 0;
  anchor_text_start = 0;
  anchor_href[0] = 0;

  for(i = 0; body[i]; i++) {
    char c;
    c = body[i];

    if(in_tag) {
      if(c == '>') {
        char name[32];
        int is_close;
        int name_off;

        name_off = html_tag_name(tagbuf, tlen, name, sizeof(name), &is_close);

        if(!is_close && strcmp(name, "br") == 0)
          text_append_char(p, '\n');
        if(!is_close && (strcmp(name, "p") == 0 || strcmp(name, "div") == 0 ||
                         strcmp(name, "li") == 0 || strcmp(name, "tr") == 0 ||
                         strcmp(name, "h1") == 0 || strcmp(name, "h2") == 0 ||
                         strcmp(name, "h3") == 0 || strcmp(name, "h4") == 0))
          text_ensure_blank_line(p);
        if(is_close && (strcmp(name, "p") == 0 || strcmp(name, "div") == 0 ||
                        strcmp(name, "li") == 0 || strcmp(name, "tr") == 0 ||
                        strcmp(name, "h1") == 0 || strcmp(name, "h2") == 0 ||
                        strcmp(name, "h3") == 0 || strcmp(name, "h4") == 0 ||
                        strcmp(name, "a") == 0))
          text_append_char(p, '\n');

        if(!is_close && strcmp(name, "a") == 0) {
          char href[LINK_URL_MAX + 1];
          href[0] = 0;
          if(html_find_href(tagbuf + name_off, tlen - name_off, href, sizeof(href)) == 0) {
            in_anchor = 1;
            anchor_text_start = p->text_len;
            snprintf(anchor_href, sizeof(anchor_href), "%s", href);
          }
        }

        if(is_close && strcmp(name, "a") == 0 && in_anchor) {
          char label[LINK_LABEL_MAX + 1];
          int n;
          int j;

          n = p->text_len - anchor_text_start;
          if(n < 1)
            n = 0;
          if(n > LINK_LABEL_MAX)
            n = LINK_LABEL_MAX;
          for(j = 0; j < n; j++)
            label[j] = p->text[anchor_text_start + j];
          label[n] = 0;
          link_add(p, label, anchor_href);
          in_anchor = 0;
          anchor_href[0] = 0;
        }

        in_tag = 0;
        tlen = 0;
      } else if(tlen < (int)sizeof(tagbuf) - 1) {
        tagbuf[tlen++] = c;
      }
      continue;
    }

    if(c == '<') {
      in_tag = 1;
      tlen = 0;
      continue;
    }

    if(c == '&') {
      int j;
      j = i + 1;
      while(body[j] && body[j] != ';' && (j - (i + 1)) < 16)
        j++;
      if(body[j] == ';') {
        html_emit_entity(p, body + i + 1, j - (i + 1));
        i = j;
        continue;
      }
    }

    if(c == '\r')
      continue;
    if(c == '\n') {
      text_append_char(p, '\n');
      continue;
    }
    if(c == '\t') {
      text_append_char(p, ' ');
      continue;
    }
    text_append_char(p, c);
  }
}

static void
render_plain(struct page_model *p, const char *body)
{
  int i;

  page_reset(p);
  for(i = 0; body[i]; i++) {
    if(body[i] == '\r')
      continue;
    text_append_char(p, body[i]);
  }
}

static void
render_body_to_page(struct page_model *p, const char *body)
{
  if(contains_nocase(p->content_type, "text/html")) {
    render_html(p, body);
    return;
  }
  if(contains_nocase(p->content_type, "text/markdown") ||
     contains_nocase(p->content_type, "text/x-markdown") ||
     contains_nocase(p->page_url, ".md")) {
    render_markdown(p, body);
    return;
  }
  render_plain(p, body);
}

static void
layout_rebuild(int cols)
{
  int i;
  int line_start;
  int line_cols;

  if(cols < 8)
    cols = 8;
  g_layout_cols = cols;
  g_line_count = 0;

  line_start = 0;
  line_cols = 0;

  for(i = 0; i < g_page.text_len; i++) {
    char c;
    c = g_page.text[i];

    if(c == '\n') {
      if(g_line_count < MAX_LINES) {
        g_line_start[g_line_count] = line_start;
        g_line_len[g_line_count] = i - line_start;
        g_line_count++;
      }
      line_start = i + 1;
      line_cols = 0;
      continue;
    }

    line_cols++;
    if(line_cols >= cols) {
      if(g_line_count < MAX_LINES) {
        g_line_start[g_line_count] = line_start;
        g_line_len[g_line_count] = (i + 1) - line_start;
        g_line_count++;
      }
      line_start = i + 1;
      line_cols = 0;
    }
  }

  if(line_start <= g_page.text_len && g_line_count < MAX_LINES) {
    g_line_start[g_line_count] = line_start;
    g_line_len[g_line_count] = g_page.text_len - line_start;
    g_line_count++;
  }

  if(g_scroll < 0)
    g_scroll = 0;
  if(g_scroll > g_line_count - 1)
    g_scroll = g_line_count > 0 ? g_line_count - 1 : 0;
}

static void
draw_line_text(int row, int col, const char *s, int n)
{
  int i;

  term_move(&g_ts, row, col);
  if(n > 0)
    term_write(&g_ts, s, n);
  for(i = n; i < g_ts.cols - col; i++)
    term_puts(&g_ts, " ");
}

static void
render_screen(void)
{
  int row;
  int content_top;
  int content_bottom;
  int i;

  content_top = 2;
  content_bottom = g_ts.rows - 2;
  if(content_bottom < content_top)
    content_bottom = content_top;

  if(g_layout_cols != g_ts.cols)
    layout_rebuild(g_ts.cols);

  term_move(&g_ts, 0, 0);
  term_attr(&g_ts, TERM_REVERSE);
  {
    char hdr[256];
    snprintf(hdr, sizeof(hdr), " abrowse  %s", g_page.page_url[0] ? g_page.page_url : "(no page)");
    if((int)strlen(hdr) > g_ts.cols)
      hdr[g_ts.cols] = 0;
    term_puts(&g_ts, hdr);
    term_clreol(&g_ts);
  }
  term_reset_attrs(&g_ts);

  term_move(&g_ts, 1, 0);
  {
    char meta[256];
    if(g_page.nlinks > 0 && g_selected_link >= 0 && g_selected_link < g_page.nlinks) {
      snprintf(meta, sizeof(meta), " type=%s  links=%d  selected=[%d] %s",
               g_page.content_type[0] ? g_page.content_type : "unknown",
               g_page.nlinks, g_selected_link + 1,
               g_page.links[g_selected_link].url);
    } else {
      snprintf(meta, sizeof(meta), " type=%s  links=%d",
               g_page.content_type[0] ? g_page.content_type : "unknown",
               g_page.nlinks);
    }
    if((int)strlen(meta) > g_ts.cols)
      meta[g_ts.cols] = 0;
    term_puts(&g_ts, meta);
    term_clreol(&g_ts);
  }

  for(row = content_top; row <= content_bottom; row++) {
    int line_idx;
    line_idx = g_scroll + (row - content_top);
    if(line_idx >= 0 && line_idx < g_line_count) {
      draw_line_text(row, 0, g_page.text + g_line_start[line_idx], g_line_len[line_idx]);
    } else {
      term_move(&g_ts, row, 0);
      term_clreol(&g_ts);
    }
  }

  term_move(&g_ts, g_ts.rows - 1, 0);
  term_attr(&g_ts, TERM_DIM);
  {
    char sbuf[256];
    snprintf(sbuf, sizeof(sbuf),
             "j/k scroll  space/b page  [/ ] link  enter open  g goto  r reload  q quit | %s",
             g_status);
    if((int)strlen(sbuf) > g_ts.cols)
      sbuf[g_ts.cols] = 0;
    term_puts(&g_ts, sbuf);
    term_clreol(&g_ts);
  }
  term_reset_attrs(&g_ts);

  if(g_page.nlinks > 0 && g_selected_link >= 0 && g_selected_link < g_page.nlinks) {
    int panel_row;
    panel_row = g_ts.rows - 3;
    if(panel_row >= content_top) {
      char lbuf[256];
      snprintf(lbuf, sizeof(lbuf), "Link[%d]: %s", g_selected_link + 1, g_page.links[g_selected_link].label);
      term_move(&g_ts, panel_row, 0);
      term_attr(&g_ts, TERM_BOLD);
      term_puts(&g_ts, lbuf);
      term_clreol(&g_ts);
      term_reset_attrs(&g_ts);
    }
  }

  for(i = 0; i < 1; i++)
    ;
}

static int
decode_key(void)
{
  int k;

  k = term_read_key(&g_ts);
  if(k != 27)
    return k;

  k = term_poll_key(&g_ts, 10);
  if(k != '[')
    return 27;

  k = term_poll_key(&g_ts, 10);
  if(k == 'A')
    return 'k';
  if(k == 'B')
    return 'j';
  if(k == '5') {
    if(term_poll_key(&g_ts, 10) == '~')
      return 'b';
  }
  if(k == '6') {
    if(term_poll_key(&g_ts, 10) == '~')
      return ' ';
  }

  return 27;
}

static int
prompt_line(const char *label, char *out, int outsz)
{
  int len;

  len = 0;
  out[0] = 0;

  term_show_cursor(&g_ts);
  while(1) {
    int k;
    char line[320];

    term_move(&g_ts, g_ts.rows - 1, 0);
    term_attr(&g_ts, TERM_REVERSE);
    snprintf(line, sizeof(line), "%s%s", label, out);
    if((int)strlen(line) > g_ts.cols)
      line[g_ts.cols] = 0;
    term_puts(&g_ts, line);
    term_clreol(&g_ts);
    term_reset_attrs(&g_ts);

    k = term_read_key(&g_ts);
    if(k == '\r' || k == '\n')
      break;
    if(k == 27) {
      out[0] = 0;
      term_hide_cursor(&g_ts);
      return -1;
    }
    if(k == 127 || k == 8) {
      if(len > 0)
        out[--len] = 0;
      continue;
    }
    if(k >= 32 && k <= 126) {
      if(len < outsz - 1) {
        out[len++] = (char)k;
        out[len] = 0;
      }
    }
  }

  term_hide_cursor(&g_ts);
  return len > 0 ? 0 : -1;
}

static int
compose_follow_url(const char *base_url, const char *href, char *out, int outsz)
{
  struct http_url base;
  struct http_url next;
  int rc;

  if(href == 0 || href[0] == 0)
    return -1;

  if(strncmp(href, "http://", 7) == 0) {
    if((int)strlen(href) >= outsz)
      return -1;
    strcpy(out, href);
    return 0;
  }

  if(strncmp(href, "https://", 8) == 0)
    return -2;
  if(strncmp(href, "gemini://", 9) == 0)
    return -3;
  if(starts_with_nocase(href, "mailto:") || starts_with_nocase(href, "javascript:"))
    return -1;

  if(parse_url(base_url, &base) < 0)
    return -1;

  rc = resolve_redirect_url(&base, href, &next);
  if(rc < 0)
    return rc;
  url_to_string(&next, out, outsz);
  return 0;
}

static int
load_page(const char *url)
{
  char err[128];

  status_set("loading...");
  render_screen();

  if(fetch_url(url, &g_page, err, sizeof(err)) < 0) {
    page_reset(&g_page);
    snprintf(g_page.page_url, sizeof(g_page.page_url), "%s", url);
    snprintf(g_page.content_type, sizeof(g_page.content_type), "text/plain");
    text_append_str(&g_page, "abrowse error: ");
    text_append_str(&g_page, err);
    text_append_str(&g_page, "\n");
    text_append_str(&g_page, "notes:\n");
    text_append_str(&g_page, " - only http:// is supported right now\n");
    text_append_str(&g_page, " - https:// and gemini:// both require TLS support\n");
    layout_rebuild(g_ts.cols);
    g_selected_link = -1;
    g_scroll = 0;
    status_set(err);
    return -1;
  }

  render_body_to_page(&g_page, g_body_buf);
  layout_rebuild(g_ts.cols);
  g_scroll = 0;
  if(g_page.nlinks > 0)
    g_selected_link = 0;
  else
    g_selected_link = -1;

  status_set("ok");
  return 0;
}

static void
usage(void)
{
  printf("usage: abrowse http://host[:port]/path\n");
  printf("       abrowse -V\n");
}

int
main(int argc, char **argv)
{
  char cur_url[URL_STR_MAX + 1];
  int quit;

  if(argc != 2) {
    usage();
    exit(1);
  }

  if(strcmp(argv[1], "-V") == 0 || strcmp(argv[1], "--version") == 0) {
    printf("%s\n", ABROWSE_VERSION);
    exit(0);
  }

  term_init(&g_ts, 0, 1);
  if(term_enter(&g_ts) < 0) {
    printf("abrowse: failed to enter terminal mode\n");
    exit(1);
  }

  g_layout_cols = 0;
  g_scroll = 0;
  g_selected_link = -1;
  status_set("ready");
  page_reset(&g_page);

  snprintf(cur_url, sizeof(cur_url), "%s", argv[1]);
  load_page(cur_url);

  quit = 0;
  while(!quit) {
    int k;

    term_update_size(&g_ts);
    render_screen();

    k = decode_key();

    if(k == 'q' || k == 'Q') {
      quit = 1;
      continue;
    }

    if(k == 'j') {
      if(g_scroll + 1 < g_line_count)
        g_scroll++;
      continue;
    }

    if(k == 'k') {
      if(g_scroll > 0)
        g_scroll--;
      continue;
    }

    if(k == ' ') {
      int step;
      step = g_ts.rows - 4;
      if(step < 1)
        step = 1;
      g_scroll += step;
      if(g_scroll > g_line_count - 1)
        g_scroll = g_line_count > 0 ? g_line_count - 1 : 0;
      continue;
    }

    if(k == 'b') {
      int step;
      step = g_ts.rows - 4;
      if(step < 1)
        step = 1;
      g_scroll -= step;
      if(g_scroll < 0)
        g_scroll = 0;
      continue;
    }

    if(k == '[') {
      if(g_page.nlinks > 0) {
        g_selected_link--;
        if(g_selected_link < 0)
          g_selected_link = g_page.nlinks - 1;
      }
      continue;
    }

    if(k == ']') {
      if(g_page.nlinks > 0) {
        g_selected_link++;
        if(g_selected_link >= g_page.nlinks)
          g_selected_link = 0;
      }
      continue;
    }

    if(k == 'r' || k == 'R') {
      load_page(cur_url);
      continue;
    }

    if(k == 'g' || k == 'G') {
      char next[URL_STR_MAX + 1];
      if(prompt_line("goto URL: ", next, sizeof(next)) == 0) {
        snprintf(cur_url, sizeof(cur_url), "%s", next);
        load_page(cur_url);
      } else {
        status_set("goto canceled");
      }
      continue;
    }

    if(k == '\r' || k == '\n') {
      if(g_page.nlinks > 0 && g_selected_link >= 0 && g_selected_link < g_page.nlinks) {
        char next_url[URL_STR_MAX + 1];
        int rc;

        rc = compose_follow_url(g_page.page_url, g_page.links[g_selected_link].url,
                                next_url, sizeof(next_url));
        if(rc == 0) {
          snprintf(cur_url, sizeof(cur_url), "%s", next_url);
          load_page(cur_url);
        } else if(rc == -2) {
          status_set("https link not supported yet");
        } else if(rc == -3) {
          status_set("gemini link not supported yet");
        } else {
          status_set("cannot follow this link");
        }
      } else {
        status_set("no selected link");
      }
      continue;
    }
  }

  term_leave(&g_ts);
  exit(0);
}
