#include "../include/types.h"
#include "../include/user.h"
#include "../include/fcntl.h"
#include "../include/socket.h"
#include "../include/net.h"

#define DNS_PORT 53
#define DNS_MAX_SERVERS 3
#define DNS_MAX_PACKET 512
#define DNS_TIMEOUT_TICKS 50
#define DNS_RETRIES 2

#define DNS_FLAG_QR 0x8000
#define DNS_FLAG_RD 0x0100
#define DNS_FLAG_RCODE 0x000f

#define DNS_TYPE_A 1
#define DNS_CLASS_IN 1

struct dns_hdr {
  ushort id;
  ushort flags;
  ushort qdcount;
  ushort ancount;
  ushort nscount;
  ushort arcount;
} __attribute__((packed));

static int
is_space(char c)
{
  return c == ' ' || c == '\t' || c == '\r' || c == '\n';
}

static int
parse_ipv4_literal(const char *s, uint *out)
{
  int i;
  int part;
  uint ip;

  if(s == 0 || out == 0)
    return -1;

  ip = 0;
  for(i = 0; i < 4; i++) {
    if(*s < '0' || *s > '9')
      return -1;

    part = 0;
    while(*s >= '0' && *s <= '9') {
      part = part * 10 + (*s - '0');
      if(part > 255)
        return -1;
      s++;
    }

    ip = (ip << 8) | (uint)part;
    if(i < 3) {
      if(*s != '.')
        return -1;
      s++;
    }
  }

  if(*s != '\0')
    return -1;

  *out = ip;
  return 0;
}

static int
read_text_file(const char *path, char *buf, int size)
{
  int fd;
  int n;

  if(path == 0 || buf == 0 || size <= 1)
    return -1;

  fd = open(path, O_RDONLY);
  if(fd < 0)
    return -1;

  n = read(fd, buf, size - 1);
  close(fd);
  if(n < 0)
    return -1;

  buf[n] = 0;
  return n;
}

static char*
next_token(char **cursor)
{
  char *p;
  char *start;

  if(cursor == 0 || *cursor == 0)
    return 0;

  p = *cursor;
  while(*p && is_space(*p))
    p++;
  if(*p == 0) {
    *cursor = p;
    return 0;
  }

  start = p;
  while(*p && !is_space(*p))
    p++;
  if(*p)
    *p++ = 0;
  *cursor = p;
  return start;
}

static int
lookup_hosts(const char *name, uint *out)
{
  char buf[1024];
  int n;
  int i;

  if(name == 0 || out == 0)
    return -1;

  n = read_text_file("/etc/hosts", buf, sizeof(buf));
  if(n <= 0)
    return -1;

  i = 0;
  while(i < n) {
    char *line;
    char *cur;
    char *tok;
    char *ipstr;
    uint ip;

    line = buf + i;
    while(i < n && buf[i] != '\n')
      i++;
    if(i < n)
      buf[i++] = 0;

    tok = strchr(line, '#');
    if(tok)
      *tok = 0;

    cur = line;
    ipstr = next_token(&cur);
    if(ipstr == 0)
      continue;
    if(parse_ipv4_literal(ipstr, &ip) < 0)
      continue;

    while((tok = next_token(&cur)) != 0) {
      if(strcmp(tok, name) == 0) {
        *out = ip;
        return 0;
      }
    }
  }

  return -1;
}

int
dns_nameservers(uint *servers, int max)
{
  char buf[512];
  int n;
  int i;
  int count;

  if(servers == 0 || max <= 0)
    return -1;

  n = read_text_file("/etc/resolv.conf", buf, sizeof(buf));
  if(n <= 0)
    return -1;

  count = 0;
  i = 0;
  while(i < n && count < max) {
    char *line;
    char *cur;
    char *tok;
    uint ip;

    line = buf + i;
    while(i < n && buf[i] != '\n')
      i++;
    if(i < n)
      buf[i++] = 0;

    tok = strchr(line, '#');
    if(tok)
      *tok = 0;

    cur = line;
    tok = next_token(&cur);
    if(tok == 0 || strcmp(tok, "nameserver") != 0)
      continue;

    tok = next_token(&cur);
    if(tok == 0)
      continue;
    if(parse_ipv4_literal(tok, &ip) < 0)
      continue;

    servers[count++] = ip;
  }

  return count;
}

static ushort
dns_get_u16(uchar *p)
{
  return (ushort)(((ushort)p[0] << 8) | (ushort)p[1]);
}

static int
dns_put_u16(uchar *p, ushort v)
{
  p[0] = (uchar)(v >> 8);
  p[1] = (uchar)(v & 0xff);
  return 2;
}

static int
dns_encode_name(uchar *dst, int max, const char *name)
{
  const char *label;
  int labellen;
  int off;
  char c;

  if(dst == 0 || max <= 0 || name == 0 || *name == 0)
    return -1;

  off = 0;
  label = name;
  labellen = 0;
  for(;;) {
    c = *name;
    if(c == '.' || c == 0) {
      if(labellen <= 0 || labellen > 63)
        return -1;
      if(off + 1 + labellen >= max)
        return -1;
      dst[off++] = (uchar)labellen;
      memmove(dst + off, label, labellen);
      off += labellen;
      if(c == 0)
        break;
      name++;
      if(*name == 0)
        return -1;
      label = name;
      labellen = 0;
      continue;
    }
    if(c == ' ' || c == '\t' || c == '\r' || c == '\n')
      return -1;
    labellen++;
    name++;
  }

  if(off >= max)
    return -1;
  dst[off++] = 0;
  return off;
}

static int
dns_skip_name(uchar *msg, int len, int off)
{
  int count;
  uchar c;

  count = 0;
  while(off < len) {
    c = msg[off];
    if(c == 0)
      return off + 1;
    if((c & 0xc0) == 0xc0) {
      if(off + 1 >= len)
        return -1;
      return off + 2;
    }
    if((c & 0xc0) != 0)
      return -1;
    off++;
    if(off + c > len)
      return -1;
    off += c;
    if(++count > 128)
      return -1;
  }
  return -1;
}

static int
dns_build_query(uchar *pkt, int max, ushort id, const char *name)
{
  struct dns_hdr *hdr;
  int off;
  int n;

  if(pkt == 0 || max < (int)sizeof(struct dns_hdr) + 6)
    return -1;

  memset(pkt, 0, max);
  hdr = (struct dns_hdr*)pkt;
  hdr->id = net_htons(id);
  hdr->flags = net_htons(DNS_FLAG_RD);
  hdr->qdcount = net_htons(1);

  off = sizeof(*hdr);
  n = dns_encode_name(pkt + off, max - off, name);
  if(n < 0)
    return -1;
  off += n;
  off += dns_put_u16(pkt + off, DNS_TYPE_A);
  off += dns_put_u16(pkt + off, DNS_CLASS_IN);
  return off;
}

static int
dns_parse_response(uchar *pkt, int len, ushort id, uint *out)
{
  struct dns_hdr *hdr;
  int off;
  int i;
  int qdcount;
  int ancount;
  ushort flags;
  ushort type;
  ushort class;
  ushort rdlen;

  if(pkt == 0 || out == 0 || len < (int)sizeof(struct dns_hdr))
    return -1;

  hdr = (struct dns_hdr*)pkt;
  if(net_ntohs(hdr->id) != id)
    return -1;

  flags = net_ntohs(hdr->flags);
  if((flags & DNS_FLAG_QR) == 0)
    return -1;
  if((flags & DNS_FLAG_RCODE) != 0)
    return -1;

  qdcount = net_ntohs(hdr->qdcount);
  ancount = net_ntohs(hdr->ancount);
  off = sizeof(*hdr);

  for(i = 0; i < qdcount; i++) {
    off = dns_skip_name(pkt, len, off);
    if(off < 0 || off + 4 > len)
      return -1;
    off += 4;
  }

  for(i = 0; i < ancount; i++) {
    off = dns_skip_name(pkt, len, off);
    if(off < 0 || off + 10 > len)
      return -1;

    type = dns_get_u16(pkt + off);
    class = dns_get_u16(pkt + off + 2);
    rdlen = dns_get_u16(pkt + off + 8);
    off += 10;
    if(off + rdlen > len)
      return -1;

    if(type == DNS_TYPE_A && class == DNS_CLASS_IN && rdlen == 4) {
      *out = ((uint)pkt[off] << 24) | ((uint)pkt[off + 1] << 16) |
             ((uint)pkt[off + 2] << 8) | (uint)pkt[off + 3];
      return 0;
    }

    off += rdlen;
  }

  return -1;
}

int
dns_lookup_ipv4(const char *name, uint server, uint *out)
{
  uchar pkt[DNS_MAX_PACKET];
  struct sockaddr_in src;
  struct sockaddr_in dst;
  ushort id;
  int fd;
  int pktlen;
  int n;
  int attempt;

  if(server == 0 || name == 0 || out == 0)
    return -1;

  fd = socket(AF_INET, SOCK_DGRAM, 0);
  if(fd < 0)
    return -1;

  memset(&src, 0, sizeof(src));
  src.sin_family = AF_INET;
  src.sin_port = (ushort)(49152 + (((uint)uptime() + (uint)getpid()) & 0x3fff));
  src.sin_addr = INADDR_ANY;
  if(bind(fd, &src, sizeof(src)) < 0) {
    close(fd);
    return -1;
  }

  memset(&dst, 0, sizeof(dst));
  dst.sin_family = AF_INET;
  dst.sin_port = DNS_PORT;
  dst.sin_addr = server;
  if(connect(fd, &dst, sizeof(dst)) < 0) {
    close(fd);
    return -1;
  }

  for(attempt = 0; attempt < DNS_RETRIES; attempt++) {
    id = (ushort)(((uint)getpid() << 8) ^ (uint)uptime() ^ (uint)attempt ^ server);
    pktlen = dns_build_query(pkt, sizeof(pkt), id, name);
    if(pktlen < 0)
      break;
    if(send(fd, pkt, pktlen) < 0)
      continue;
    n = recvtimeout(fd, pkt, sizeof(pkt), DNS_TIMEOUT_TICKS);
    if(n <= 0)
      continue;
    if(dns_parse_response(pkt, n, id, out) == 0) {
      close(fd);
      return 0;
    }
  }

  close(fd);
  return -1;
}

int
resolve_ipv4(const char *name, uint *out)
{
  uint servers[DNS_MAX_SERVERS];
  int i;
  int nservers;

  if(name == 0 || out == 0)
    return -1;

  if(parse_ipv4_literal(name, out) == 0)
    return 0;
  if(lookup_hosts(name, out) == 0)
    return 0;

  nservers = dns_nameservers(servers, DNS_MAX_SERVERS);
  if(nservers <= 0)
    return -1;

  for(i = 0; i < nservers; i++) {
    if(dns_lookup_ipv4(name, servers[i], out) == 0)
      return 0;
  }

  return -1;
}