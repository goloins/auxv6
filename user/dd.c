#include "types.h"
#include "stat.h"
#include "fcntl.h"
#include "errno.h"
#include "limits.h"
#include "stdlib.h"
#include "string.h"
#include "auxv6/user.h"

struct dd_opts {
  const char *if_path;
  const char *of_path;
  uint ibs;
  uint obs;
  unsigned long long count;
  int have_count;
  unsigned long long skip;
  unsigned long long seek;
  int conv_notrunc;
  int conv_sync;
  int conv_noerror;
  int iflag_fullblock;
  int oflag_append;
  int status_none;
  int status_noxfer;
};

struct dd_stats {
  unsigned long long in_full;
  unsigned long long in_part;
  unsigned long long out_full;
  unsigned long long out_part;
  unsigned long long bytes_written;
};

static void
usage(void)
{
  dprintf(2,
          "usage: dd [if=file] [of=file] [bs=n] [ibs=n] [obs=n] [count=n] [skip=n] [seek=n]\\n"
          "          [conv=notrunc,sync,noerror] [iflag=fullblock] [oflag=append]\\n"
          "          [status=none|noxfer]\\n");
  exit(1);
}

static int
is_mul_sep(char c)
{
  return c == 'x' || c == 'X' || c == '*';
}

static int
parse_factor(const char *s, unsigned long long *out)
{
  char *end;
  unsigned long long n;
  unsigned long long mul;

  if(s == 0 || *s == '\0')
    return -1;

  n = strtoull(s, &end, 0);
  if(end == s)
    return -1;

  mul = 1ULL;
  if(*end != '\0') {
    if(strcmp(end, "c") == 0)
      mul = 1ULL;
    else if(strcmp(end, "w") == 0)
      mul = 2ULL;
    else if(strcmp(end, "b") == 0)
      mul = 512ULL;
    else if(strcmp(end, "k") == 0 || strcmp(end, "K") == 0)
      mul = 1024ULL;
    else if(strcmp(end, "kB") == 0 || strcmp(end, "KB") == 0)
      mul = 1000ULL;
    else if(strcmp(end, "m") == 0 || strcmp(end, "M") == 0)
      mul = 1024ULL * 1024ULL;
    else if(strcmp(end, "MB") == 0)
      mul = 1000ULL * 1000ULL;
    else if(strcmp(end, "g") == 0 || strcmp(end, "G") == 0)
      mul = 1024ULL * 1024ULL * 1024ULL;
    else if(strcmp(end, "GB") == 0)
      mul = 1000ULL * 1000ULL * 1000ULL;
    else
      return -1;
  }

  if(n != 0 && mul > ULLONG_MAX / n)
    return -1;
  *out = n * mul;
  return 0;
}

static int
parse_num(const char *s, unsigned long long *out)
{
  unsigned long long total;
  const char *p;
  const char *seg;
  char part[64];

  if(s == 0 || *s == '\0')
    return -1;

  total = 1ULL;
  p = s;
  while(1) {
    int len;
    unsigned long long factor;

    seg = p;
    while(*p && !is_mul_sep(*p))
      p++;
    len = p - seg;
    if(len <= 0 || len >= (int)sizeof(part))
      return -1;
    memmove(part, seg, len);
    part[len] = '\0';

    if(parse_factor(part, &factor) < 0)
      return -1;
    if(factor != 0 && total > ULLONG_MAX / factor)
      return -1;
    total *= factor;

    if(*p == '\0')
      break;
    p++;
    if(*p == '\0')
      return -1;
  }

  *out = total;
  return 0;
}

static int
parse_size_u32(const char *arg, const char *name, uint *out)
{
  unsigned long long n;

  if(parse_num(arg, &n) < 0 || n == 0 || n > UINT_MAX) {
    dprintf(2, "dd: invalid %s: %s\n", name, arg);
    return -1;
  }

  *out = (uint)n;
  return 0;
}

static int
parse_count_arg(const char *arg, const char *name, unsigned long long *out)
{
  if(parse_num(arg, out) < 0) {
    dprintf(2, "dd: invalid %s: %s\n", name, arg);
    return -1;
  }
  return 0;
}

static int
parse_csv_flags(const char *csv, int (*cb)(const char *tok, void *ctx), void *ctx)
{
  char buf[128];
  int i;
  int j;

  if(csv == 0 || *csv == '\0')
    return -1;

  i = 0;
  while(csv[i]) {
    int start;

    start = i;
    while(csv[i] && csv[i] != ',')
      i++;

    if(i - start <= 0 || i - start >= (int)sizeof(buf))
      return -1;

    for(j = 0; j < i - start; j++)
      buf[j] = csv[start + j];
    buf[j] = '\0';

    if(cb(buf, ctx) < 0)
      return -1;

    if(csv[i] == ',')
      i++;
  }

  return 0;
}

static int
conv_flag_cb(const char *tok, void *ctx)
{
  struct dd_opts *o;

  o = (struct dd_opts *)ctx;

  if(strcmp(tok, "notrunc") == 0)
    o->conv_notrunc = 1;
  else if(strcmp(tok, "sync") == 0)
    o->conv_sync = 1;
  else if(strcmp(tok, "noerror") == 0)
    o->conv_noerror = 1;
  else
    return -1;

  return 0;
}

static int
iflag_flag_cb(const char *tok, void *ctx)
{
  struct dd_opts *o;

  o = (struct dd_opts *)ctx;

  if(strcmp(tok, "fullblock") == 0)
    o->iflag_fullblock = 1;
  else
    return -1;

  return 0;
}

static int
oflag_flag_cb(const char *tok, void *ctx)
{
  struct dd_opts *o;

  o = (struct dd_opts *)ctx;

  if(strcmp(tok, "append") == 0)
    o->oflag_append = 1;
  else
    return -1;

  return 0;
}

static int
status_flag_cb(const char *tok, void *ctx)
{
  struct dd_opts *o;

  o = (struct dd_opts *)ctx;

  if(strcmp(tok, "none") == 0)
    o->status_none = 1;
  else if(strcmp(tok, "noxfer") == 0)
    o->status_noxfer = 1;
  else
    return -1;

  return 0;
}

static int
parse_arg(struct dd_opts *o, const char *arg)
{
  const char *eq;
  int klen;

  eq = strchr(arg, '=');
  if(eq == 0)
    return -1;
  klen = eq - arg;
  if(klen <= 0)
    return -1;

  if(klen == 2 && strncmp(arg, "if", 2) == 0) {
    o->if_path = eq + 1;
    return (o->if_path[0] == '\0') ? -1 : 0;
  }
  if(klen == 2 && strncmp(arg, "of", 2) == 0) {
    o->of_path = eq + 1;
    return (o->of_path[0] == '\0') ? -1 : 0;
  }

  if(klen == 2 && strncmp(arg, "bs", 2) == 0) {
    if(parse_size_u32(eq + 1, "bs", &o->ibs) < 0)
      return -1;
    o->obs = o->ibs;
    return 0;
  }
  if(klen == 3 && strncmp(arg, "ibs", 3) == 0)
    return parse_size_u32(eq + 1, "ibs", &o->ibs);
  if(klen == 3 && strncmp(arg, "obs", 3) == 0)
    return parse_size_u32(eq + 1, "obs", &o->obs);

  if(klen == 5 && strncmp(arg, "count", 5) == 0) {
    if(parse_count_arg(eq + 1, "count", &o->count) < 0)
      return -1;
    o->have_count = 1;
    return 0;
  }
  if(klen == 4 && strncmp(arg, "skip", 4) == 0)
    return parse_count_arg(eq + 1, "skip", &o->skip);
  if(klen == 4 && strncmp(arg, "seek", 4) == 0)
    return parse_count_arg(eq + 1, "seek", &o->seek);

  if(klen == 4 && strncmp(arg, "conv", 4) == 0) {
    if(parse_csv_flags(eq + 1, conv_flag_cb, o) < 0) {
      dprintf(2, "dd: unsupported conv option: %s\n", eq + 1);
      return -1;
    }
    return 0;
  }

  if(klen == 5 && strncmp(arg, "iflag", 5) == 0) {
    if(parse_csv_flags(eq + 1, iflag_flag_cb, o) < 0) {
      dprintf(2, "dd: unsupported iflag option: %s\n", eq + 1);
      return -1;
    }
    return 0;
  }

  if(klen == 5 && strncmp(arg, "oflag", 5) == 0) {
    if(parse_csv_flags(eq + 1, oflag_flag_cb, o) < 0) {
      dprintf(2, "dd: unsupported oflag option: %s\n", eq + 1);
      return -1;
    }
    return 0;
  }

  if(klen == 6 && strncmp(arg, "status", 6) == 0) {
    if(parse_csv_flags(eq + 1, status_flag_cb, o) < 0) {
      dprintf(2, "dd: unsupported status option: %s\n", eq + 1);
      return -1;
    }
    return 0;
  }

  return -1;
}

static int
read_discard_loop(int fd, unsigned long long blocks, uint ibs, char *buf)
{
  unsigned long long i;

  for(i = 0; i < blocks; i++) {
    uint left;

    left = ibs;
    while(left > 0) {
      int want;
      int n;

      want = (left > 4096U) ? 4096 : (int)left;
      n = read(fd, buf, want);
      if(n == 0)
        return 0;
      if(n < 0)
        return -1;
      left -= (uint)n;
    }
  }

  return 0;
}

static int
skip_input_blocks(int fd, unsigned long long blocks, uint ibs, char *buf)
{
  unsigned long long bytes;

  if(blocks == 0)
    return 0;

  bytes = blocks * (unsigned long long)ibs;
  if(ibs != 0 && bytes / (unsigned long long)ibs != blocks)
    return -1;

  if(bytes <= (unsigned long long)INT_MAX) {
    if(lseek(fd, (off_t)bytes, SEEK_CUR) >= 0)
      return 0;
  }

  return read_discard_loop(fd, blocks, ibs, buf);
}

static int
seek_output_blocks(int fd, unsigned long long blocks, uint obs)
{
  unsigned long long bytes;

  if(blocks == 0)
    return 0;

  bytes = blocks * (unsigned long long)obs;
  if(obs != 0 && bytes / (unsigned long long)obs != blocks)
    return -1;

  if(bytes > (unsigned long long)INT_MAX)
    return -1;

  return lseek(fd, (off_t)bytes, SEEK_CUR) < 0 ? -1 : 0;
}

static int
read_block(int fd, char *buf, uint ibs, int fullblock)
{
  if(!fullblock)
    return read(fd, buf, ibs);

  {
    uint got;

    got = 0;
    while(got < ibs) {
      int n = read(fd, buf + got, ibs - got);
      if(n < 0)
        return -1;
      if(n == 0)
        break;
      got += (uint)n;
    }
    return (int)got;
  }
}

static int
write_all(int fd, const char *buf, uint n)
{
  uint off;

  off = 0;
  while(off < n) {
    int w = write(fd, buf + off, n - off);
    if(w <= 0)
      return -1;
    off += (uint)w;
  }

  return 0;
}

static int
flush_outbuf(int fd, char *obuf, uint *oused, uint obs, struct dd_stats *st)
{
  if(*oused == 0)
    return 0;

  if(write_all(fd, obuf, *oused) < 0)
    return -1;

  if(*oused == obs)
    st->out_full++;
  else
    st->out_part++;

  st->bytes_written += *oused;
  *oused = 0;
  return 0;
}

static int
queue_output(int fd, char *obuf, uint *oused, uint obs,
             const char *src, uint n, struct dd_stats *st)
{
  uint off;

  off = 0;
  while(off < n) {
    uint space;
    uint take;

    if(*oused == obs) {
      if(flush_outbuf(fd, obuf, oused, obs, st) < 0)
        return -1;
    }

    space = obs - *oused;
    take = n - off;
    if(take > space)
      take = space;

    memmove(obuf + *oused, src + off, take);
    *oused += take;
    off += take;
  }

  return 0;
}

int
main(int argc, char *argv[])
{
  struct dd_opts o;
  struct dd_stats st;
  char *ibuf;
  char *obuf;
  uint oused;
  int ifd;
  int ofd;
  int oflags;
  int i;
  unsigned long long copied_blocks;
  int had_error;

  memset(&o, 0, sizeof(o));
  o.ibs = 512;
  o.obs = 512;

  for(i = 1; i < argc; i++) {
    if(strcmp(argv[i], "--help") == 0 || strcmp(argv[i], "-h") == 0)
      usage();
    if(parse_arg(&o, argv[i]) < 0) {
      dprintf(2, "dd: invalid argument: %s\n", argv[i]);
      usage();
    }
  }

  if(o.status_none)
    o.status_noxfer = 1;

  ibuf = malloc(o.ibs);
  obuf = malloc(o.obs);
  if(ibuf == 0 || obuf == 0) {
    dprintf(2, "dd: out of memory for buffers\n");
    exit(1);
  }

  ifd = 0;
  if(o.if_path) {
    ifd = open(o.if_path, O_RDONLY);
    if(ifd < 0) {
      dprintf(2, "dd: cannot open %s\n", o.if_path);
      exit(1);
    }
  }

  ofd = 1;
  if(o.of_path) {
    oflags = O_WRONLY | O_CREATE;
    if(!o.conv_notrunc && !o.oflag_append)
      oflags |= O_TRUNC;
    if(o.oflag_append)
      oflags |= O_APPEND;

    ofd = open(o.of_path, oflags);
    if(ofd < 0) {
      dprintf(2, "dd: cannot open %s\n", o.of_path);
      if(ifd != 0)
        close(ifd);
      exit(1);
    }
  }

  if(o.skip > 0) {
    if(skip_input_blocks(ifd, o.skip, o.ibs, ibuf) < 0) {
      dprintf(2, "dd: skip failed\n");
      if(ifd != 0)
        close(ifd);
      if(ofd != 1)
        close(ofd);
      exit(1);
    }
  }

  if(o.seek > 0) {
    if(seek_output_blocks(ofd, o.seek, o.obs) < 0) {
      dprintf(2, "dd: seek failed\n");
      if(ifd != 0)
        close(ifd);
      if(ofd != 1)
        close(ofd);
      exit(1);
    }
  }

  memset(&st, 0, sizeof(st));
  oused = 0;
  copied_blocks = 0;
  had_error = 0;

  while(!o.have_count || copied_blocks < o.count) {
    int n;
    uint out_n;
    int synthetic_block;

    synthetic_block = 0;
    n = read_block(ifd, ibuf, o.ibs, o.iflag_fullblock);
    if(n < 0) {
      if(!o.conv_noerror) {
        dprintf(2, "dd: read error\n");
        had_error = 1;
        break;
      }

      dprintf(2, "dd: read error (continuing due to conv=noerror)\n");
      if(o.conv_sync) {
        memset(ibuf, 0, o.ibs);
        n = (int)o.ibs;
        synthetic_block = 1;
      } else {
        if(skip_input_blocks(ifd, 1, o.ibs, ibuf) < 0) {
          dprintf(2, "dd: cannot recover from read error\n");
          had_error = 1;
          break;
        }
        continue;
      }
    }

    if(n == 0)
      break;

    if(n == (int)o.ibs)
      st.in_full++;
    else
      st.in_part++;

    out_n = (uint)n;
    if((uint)n < o.ibs && o.conv_sync) {
      memset(ibuf + n, 0, o.ibs - (uint)n);
      out_n = o.ibs;
    }

    if(queue_output(ofd, obuf, &oused, o.obs, ibuf, out_n, &st) < 0) {
      dprintf(2, "dd: write error\n");
      break;
    }

    copied_blocks++;

    if(synthetic_block)
      continue;
  }

  if(flush_outbuf(ofd, obuf, &oused, o.obs, &st) < 0) {
    dprintf(2, "dd: write error\n");
    had_error = 1;
  }

  if(o.of_path && fsync(ofd) < 0)
    had_error = 1;

  if(!o.status_none) {
    dprintf(2, "%llu+%llu records in\n", st.in_full, st.in_part);
    dprintf(2, "%llu+%llu records out\n", st.out_full, st.out_part);
    if(!o.status_noxfer)
      dprintf(2, "%llu bytes copied\n", st.bytes_written);
  }

  if(ifd != 0)
    close(ifd);
  if(ofd != 1)
    close(ofd);

  free(ibuf);
  free(obuf);
  exit(had_error ? 1 : 0);
}
