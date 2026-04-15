#include "types.h"
#include "auxv6/user.h"
#include "auxv6/gzip.h"
#include "auxv6/bzip2.h"
#include "dirent.h"
#include "sys/stat.h"
#include "fcntl.h"
#include "unistd.h"
#include "errno.h"
#include "stdlib.h"
#include "string.h"
#include "stdio.h"

#define TAR_BLOCK 512
#define TAR_PATH_MAX 4096
#define TAR_MAX_PAX_PAYLOAD (8U * 1024U * 1024U)

#define TAR_TYPE_REG '0'
#define TAR_TYPE_AREG '\0'
#define TAR_TYPE_HARDLINK '1'
#define TAR_TYPE_SYMLINK '2'
#define TAR_TYPE_CHAR '3'
#define TAR_TYPE_BLOCK '4'
#define TAR_TYPE_DIR '5'
#define TAR_TYPE_FIFO '6'
#define TAR_TYPE_CONTIG '7'
#define TAR_TYPE_XHDR 'x'
#define TAR_TYPE_GHDR 'g'
#define TAR_TYPE_GNU_LONGLINK 'K'
#define TAR_TYPE_GNU_LONGNAME 'L'
#define TAR_TYPE_GNU_SPARSE 'S'
#define TAR_TYPE_GNU_DUMPDIR 'D'
#define TAR_TYPE_GNU_MULTIVOL 'M'
#define TAR_TYPE_GNU_NAMES 'N'
#define TAR_TYPE_GNU_VOLHDR 'V'
#define TAR_TYPE_SOLARIS_XHDR 'X'

struct tar_opts {
  int mode_create;
  int mode_extract;
  int mode_list;
  int gzip;
  int bzip2;
  int verbose;
  char *archive;
};

struct tar_header {
  char name[100];
  char mode[8];
  char uid[8];
  char gid[8];
  char size[12];
  char mtime[12];
  char chksum[8];
  char typeflag;
  char linkname[100];
  char magic[6];
  char version[2];
  char uname[32];
  char gname[32];
  char devmajor[8];
  char devminor[8];
  char prefix[155];
  char pad[12];
};

struct pax_attrs {
  char *path;
  char *linkpath;
  int have_size;
  uint64_t size;
};

struct tar_overrides {
  struct pax_attrs global;
  struct pax_attrs local;
  char *gnu_longname;
  char *gnu_longlink;
};

static int archive_skip_padding(int fd, uint64_t size);
static int skip_payload(int afd, uint64_t size);

static void
usage(void)
{
  dprintf(2,
          "usage: tar -c|-t|-x [-v] [-z] [-j] -f archive [path ...]\n"
          "       tar c|t|x[v][z|g|j]f archive [path ...]\n");
  exit(1);
}

static int
is_oldstyle_flag_word(const char *arg)
{
  int i;

  if(arg[0] == 0)
    return 0;

  for(i = 0; arg[i]; i++) {
    switch(arg[i]) {
    case 'c':
    case 't':
    case 'x':
    case 'z':
    case 'g':
    case 'j':
    case 'v':
    case 'f':
      break;
    default:
      return 0;
    }
  }

  return 1;
}

static const char*
gzip_errno_detail(int err)
{
  switch(err) {
  case ENODATA:
    return "no gzip member found in input";
  case EBADMSG:
    return "gzip header/trailer checksum mismatch or malformed member";
  case EILSEQ:
    return "invalid deflate bitstream";
  case EIO:
    return "I/O error while reading or writing compressed data";
  default:
    return "invalid or unsupported gzip archive";
  }
}

static const char*
bzip2_errno_detail(int err)
{
  if(err == EOPNOTSUPP)
    return "bzip2 support is not available in this build";
  if(err == EIO)
    return "I/O error while reading or writing compressed data";
  return "invalid or unsupported bzip2 archive";
}

static int
write_all(int fd, const void *buf, int n)
{
  const uchar *p;
  int off;

  p = (const uchar*)buf;
  off = 0;
  while(off < n) {
    int m = write(fd, p + off, n - off);
    if(m <= 0)
      return -1;
    off += m;
  }
  return 0;
}

static int
read_all(int fd, void *buf, int n)
{
  uchar *p;
  int off;

  p = (uchar*)buf;
  off = 0;
  while(off < n) {
    int m = read(fd, p + off, n - off);
    if(m <= 0)
      return -1;
    off += m;
  }
  return 0;
}

static int
digits_u64(uint64_t v)
{
  int d;

  d = 1;
  while(v >= 10ULL) {
    v /= 10ULL;
    d++;
  }
  return d;
}

static int
octal_fits(int width, uint64_t v)
{
  int digits;

  digits = width - 1;
  while(v && digits > 0) {
    v >>= 3;
    digits--;
  }
  return v == 0;
}

static int
octal_write_checked(char *dst, int width, uint64_t v)
{
  int i;

  if(!octal_fits(width, v)) {
    errno = EOVERFLOW;
    return -1;
  }

  for(i = 0; i < width; i++)
    dst[i] = '0';

  dst[width - 1] = '\0';
  for(i = width - 2; i >= 0 && v; i--) {
    dst[i] = '0' + (v & 7U);
    v >>= 3;
  }
  return 0;
}

static uint64_t
octal_parse(const char *s, int n)
{
  uint64_t v;
  int i;

  v = 0;
  i = 0;
  while(i < n && (s[i] == ' ' || s[i] == '\0'))
    i++;
  for(; i < n; i++) {
    if(s[i] < '0' || s[i] > '7')
      break;
    v = (v << 3) + (uint64_t)(s[i] - '0');
  }
  return v;
}

static int
decimal_parse_u64(const char *s, uint n, uint64_t *out)
{
  uint64_t v;
  uint i;

  if(n == 0)
    return -1;

  v = 0;
  for(i = 0; i < n; i++) {
    if(s[i] < '0' || s[i] > '9')
      return -1;
    v = v * 10ULL + (uint64_t)(s[i] - '0');
  }

  *out = v;
  return 0;
}

static int
is_all_zero(const uchar *b, int n)
{
  int i;

  for(i = 0; i < n; i++)
    if(b[i] != 0)
      return 0;
  return 1;
}

static int
tar_checksum(const struct tar_header *h)
{
  const uchar *p;
  int i;
  int sum;

  p = (const uchar*)h;
  sum = 0;
  for(i = 0; i < TAR_BLOCK; i++)
    sum += p[i];
  return sum;
}

static void
tar_finalize_header(struct tar_header *h)
{
  int i;
  int sum;

  for(i = 0; i < 8; i++)
    h->chksum[i] = ' ';

  sum = tar_checksum(h);
  octal_write_checked(h->chksum, sizeof(h->chksum), (uint64_t)sum);
  h->chksum[6] = '\0';
  h->chksum[7] = ' ';
}

static int
split_name_prefix(const char *path, char *name, int name_sz, char *prefix, int prefix_sz)
{
  int len;
  int i;

  len = strlen(path);
  if(len < name_sz) {
    memmove(name, path, len + 1);
    prefix[0] = 0;
    return 0;
  }

  for(i = len - 1; i >= 0; i--) {
    if(path[i] != '/')
      continue;
    if(i == 0)
      continue;
    if(i < prefix_sz && (len - i - 1) < name_sz) {
      memmove(prefix, path, i);
      prefix[i] = 0;
      memmove(name, path + i + 1, len - i);
      return 0;
    }
  }

  return -1;
}

static int
join_path(char *out, int out_sz, const char *a, const char *b)
{
  int na;
  int nb;

  na = strlen(a);
  nb = strlen(b);

  if(na == 0) {
    if(nb + 1 > out_sz)
      return -1;
    memmove(out, b, nb + 1);
    return 0;
  }

  if(a[na - 1] == '/') {
    if(na + nb + 1 > out_sz)
      return -1;
    memmove(out, a, na);
    memmove(out + na, b, nb + 1);
    return 0;
  }

  if(na + 1 + nb + 1 > out_sz)
    return -1;
  memmove(out, a, na);
  out[na] = '/';
  memmove(out + na + 1, b, nb + 1);
  return 0;
}

static const char*
path_basename(const char *path)
{
  const char *base;
  const char *p;

  base = path;
  for(p = path; *p; p++)
    if(*p == '/')
      base = p + 1;
  return base;
}

static void
string_clear(char **dst)
{
  if(dst && *dst) {
    free(*dst);
    *dst = 0;
  }
}

static int
string_assign_n(char **dst, const char *src, uint n)
{
  char *copy;

  copy = malloc(n + 1);
  if(copy == 0)
    return -1;
  if(n)
    memmove(copy, src, n);
  copy[n] = 0;
  string_clear(dst);
  *dst = copy;
  return 0;
}

static int
string_assign(char **dst, const char *src)
{
  return string_assign_n(dst, src, strlen(src));
}

static int
buffer_append(char **buf, uint *len, uint *cap, const void *src, uint n)
{
  char *next;
  uint want;

  want = *len + n + 1;
  if(want > *cap) {
    uint next_cap;

    next_cap = *cap ? *cap : 256U;
    while(next_cap < want)
      next_cap *= 2U;

    next = realloc(*buf, next_cap);
    if(next == 0)
      return -1;
    *buf = next;
    *cap = next_cap;
  }

  if(n)
    memmove(*buf + *len, src, n);
  *len += n;
  (*buf)[*len] = 0;
  return 0;
}

static int
append_pax_record(char **buf, uint *len, uint *cap, const char *key, const char *value)
{
  char numbuf[32];
  int body_len;
  int rec_len;
  int next_len;

  body_len = strlen(key) + 1 + strlen(value) + 1;
  rec_len = body_len + 2;
  for(;;) {
    next_len = body_len + digits_u64((uint64_t)rec_len) + 1;
    if(next_len == rec_len)
      break;
    rec_len = next_len;
  }

  snprintf(numbuf, sizeof(numbuf), "%d ", rec_len);
  if(buffer_append(buf, len, cap, numbuf, strlen(numbuf)) < 0 ||
     buffer_append(buf, len, cap, key, strlen(key)) < 0 ||
     buffer_append(buf, len, cap, "=", 1) < 0 ||
     buffer_append(buf, len, cap, value, strlen(value)) < 0 ||
     buffer_append(buf, len, cap, "\n", 1) < 0)
    return -1;
  return 0;
}

static int
append_pax_record_u64(char **buf, uint *len, uint *cap, const char *key, uint64_t value)
{
  char valbuf[32];

  snprintf(valbuf, sizeof(valbuf), "%llu", (unsigned long long)value);
  return append_pax_record(buf, len, cap, key, valbuf);
}

static void
pax_clear(struct pax_attrs *attrs)
{
  if(attrs == 0)
    return;

  string_clear(&attrs->path);
  string_clear(&attrs->linkpath);
  attrs->have_size = 0;
  attrs->size = 0;
}

static int
pax_parse_payload(int afd, uint64_t size, struct pax_attrs *attrs)
{
  char *buf;
  uint off;

  if(size > TAR_MAX_PAX_PAYLOAD) {
    if(skip_payload(afd, size) < 0)
      return -1;
    errno = EOVERFLOW;
    return -1;
  }

  buf = malloc((uint)size + 1);
  if(buf == 0)
    return -1;

  if(read_all(afd, buf, (int)size) < 0) {
    free(buf);
    return -1;
  }
  buf[(uint)size] = 0;

  if(archive_skip_padding(afd, size) < 0) {
    free(buf);
    return -1;
  }

  off = 0;
  while(off < (uint)size) {
    uint64_t rec_len_u64;
    uint rec_len;
    uint pos;
    uint key_start;
    uint value_pos;
    uint value_len;

    rec_len_u64 = 0;
    pos = off;
    while(pos < (uint)size && buf[pos] >= '0' && buf[pos] <= '9') {
      rec_len_u64 = rec_len_u64 * 10ULL + (uint64_t)(buf[pos] - '0');
      pos++;
    }

    if(rec_len_u64 == 0 || rec_len_u64 > (uint64_t)((uint)size - off) ||
       pos >= (uint)size || buf[pos] != ' ') {
      free(buf);
      errno = EBADMSG;
      return -1;
    }

    rec_len = (uint)rec_len_u64;
    if(buf[off + rec_len - 1] != '\n') {
      free(buf);
      errno = EBADMSG;
      return -1;
    }

    key_start = pos + 1;
    value_pos = key_start;
    while(value_pos < off + rec_len && buf[value_pos] != '=')
      value_pos++;
    if(value_pos >= off + rec_len - 1) {
      free(buf);
      errno = EBADMSG;
      return -1;
    }

    value_len = off + rec_len - value_pos - 2;
    if(attrs) {
      if((value_pos - key_start) == 4 && memcmp(buf + key_start, "path", 4) == 0) {
        if(string_assign_n(&attrs->path, buf + value_pos + 1, value_len) < 0) {
          free(buf);
          return -1;
        }
      } else if((value_pos - key_start) == 8 && memcmp(buf + key_start, "linkpath", 8) == 0) {
        if(string_assign_n(&attrs->linkpath, buf + value_pos + 1, value_len) < 0) {
          free(buf);
          return -1;
        }
      } else if((value_pos - key_start) == 4 && memcmp(buf + key_start, "size", 4) == 0) {
        uint64_t parsed;

        if(decimal_parse_u64(buf + value_pos + 1, value_len, &parsed) < 0) {
          free(buf);
          errno = EBADMSG;
          return -1;
        }
        attrs->size = parsed;
        attrs->have_size = 1;
      }
    }

    off += rec_len;
  }

  free(buf);
  return 0;
}

static void
overrides_clear_local(struct tar_overrides *overrides)
{
  if(overrides == 0)
    return;

  pax_clear(&overrides->local);
  string_clear(&overrides->gnu_longname);
  string_clear(&overrides->gnu_longlink);
}

static void
overrides_clear_all(struct tar_overrides *overrides)
{
  if(overrides == 0)
    return;

  pax_clear(&overrides->global);
  overrides_clear_local(overrides);
}

static int
read_payload_string(int afd, uint64_t size, char **out)
{
  char *buf;
  uint len;

  if(size > TAR_MAX_PAX_PAYLOAD) {
    if(skip_payload(afd, size) < 0)
      return -1;
    errno = EOVERFLOW;
    return -1;
  }

  buf = malloc((uint)size + 1);
  if(buf == 0)
    return -1;

  if(read_all(afd, buf, (int)size) < 0) {
    free(buf);
    return -1;
  }
  buf[(uint)size] = 0;

  if(archive_skip_padding(afd, size) < 0) {
    free(buf);
    return -1;
  }

  len = (uint)size;
  while(len > 0 && (buf[len - 1] == '\0' || buf[len - 1] == '\n'))
    len--;

  string_clear(out);
  if(string_assign_n(out, buf, len) < 0) {
    free(buf);
    return -1;
  }

  free(buf);
  return 0;
}

static int
sanitize_extract_path(const char *in, char **out)
{
  char *buf;
  int len;
  int i;
  int out_len;

  if(in == 0) {
    errno = EINVAL;
    return -1;
  }

  len = strlen(in);
  if(len > TAR_PATH_MAX) {
    errno = ENAMETOOLONG;
    return -1;
  }

  buf = malloc(len + 1);
  if(buf == 0)
    return -1;

  i = 0;
  out_len = 0;
  while(in[i]) {
    int start;
    int end;
    int part_len;

    while(in[i] == '/')
      i++;
    start = i;
    while(in[i] && in[i] != '/')
      i++;
    end = i;
    part_len = end - start;
    if(part_len == 0)
      continue;
    if(part_len == 1 && in[start] == '.')
      continue;
    if(part_len == 2 && in[start] == '.' && in[start + 1] == '.') {
      free(buf);
      errno = EINVAL;
      return -1;
    }
    if(out_len)
      buf[out_len++] = '/';
    memmove(buf + out_len, in + start, part_len);
    out_len += part_len;
  }

  buf[out_len] = 0;
  string_clear(out);
  *out = buf;
  return 0;
}

static int
archive_write_padding(int fd, uint64_t size)
{
  uchar zero[TAR_BLOCK];
  uint pad;

  memset(zero, 0, sizeof(zero));
  pad = (uint)(size % TAR_BLOCK);
  if(pad == 0)
    return 0;
  pad = TAR_BLOCK - pad;
  return write_all(fd, zero, (int)pad);
}

static int
archive_skip_padding(int fd, uint64_t size)
{
  uchar tmp[64];
  uint pad;
  uint left;

  pad = (uint)(size % TAR_BLOCK);
  if(pad == 0)
    return 0;
  left = TAR_BLOCK - pad;

  while(left) {
    int chunk = left > sizeof(tmp) ? sizeof(tmp) : (int)left;
    if(read_all(fd, tmp, chunk) < 0)
      return -1;
    left -= chunk;
  }

  return 0;
}

static int
ensure_directory_tree(const char *path)
{
  char *buf;
  int n;
  int i;

  if(path == 0 || path[0] == 0)
    return 0;

  n = strlen(path);
  if(n > TAR_PATH_MAX) {
    errno = ENAMETOOLONG;
    return -1;
  }

  buf = malloc(n + 1);
  if(buf == 0)
    return -1;
  memmove(buf, path, n + 1);

  for(i = 0; i <= n; i++) {
    struct stat st;
    char saved;

    if(buf[i] != '/' && buf[i] != 0)
      continue;

    saved = buf[i];
    buf[i] = 0;
    if(buf[0] != 0) {
      if(lstat(buf, &st) < 0) {
        if(errno != ENOENT) {
          free(buf);
          return -1;
        }
        if(mkdir(buf, 0755) < 0 && errno != EEXIST) {
          free(buf);
          return -1;
        }
      } else if(S_ISLNK(st.st_mode)) {
        free(buf);
        errno = ELOOP;
        return -1;
      } else if(!S_ISDIR(st.st_mode)) {
        free(buf);
        errno = ENOTDIR;
        return -1;
      }
    }
    buf[i] = saved;
  }

  free(buf);
  return 0;
}

static int
mkdir_parent(const char *path)
{
  char *buf;
  int i;

  if(path == 0 || path[0] == 0)
    return 0;

  if(strlen(path) > TAR_PATH_MAX) {
    errno = ENAMETOOLONG;
    return -1;
  }

  buf = malloc(strlen(path) + 1);
  if(buf == 0)
    return -1;
  strcpy(buf, path);

  for(i = strlen(buf) - 1; i >= 0; i--) {
    if(buf[i] == '/') {
      buf[i] = 0;
      break;
    }
  }

  if(i < 0 || buf[0] == 0) {
    free(buf);
    return 0;
  }

  i = ensure_directory_tree(buf);
  free(buf);
  return i;
}

static int
extract_directory(const char *path, uint mode)
{
  struct stat st;

  if(path[0] == 0)
    return 0;

  if(lstat(path, &st) == 0) {
    if(S_ISLNK(st.st_mode)) {
      errno = ELOOP;
      return -1;
    }
    if(!S_ISDIR(st.st_mode)) {
      if(unlink(path) < 0)
        return -1;
    }
  } else if(errno != ENOENT) {
    return -1;
  }

  if(ensure_directory_tree(path) < 0)
    return -1;
  if(chmod(path, mode & 07777) < 0)
    return -1;
  return 0;
}

static int
extract_regular(int afd, const char *path, uint64_t size, uint mode)
{
  int fd;
  uchar buf[1024];
  uint64_t left;
  struct stat st;

  if(mkdir_parent(path) < 0)
    return -1;

  if(lstat(path, &st) == 0) {
    if(S_ISDIR(st.st_mode)) {
      errno = EISDIR;
      return -1;
    }
    if(unlink(path) < 0)
      return -1;
  } else if(errno != ENOENT) {
    return -1;
  }

  fd = open(path, O_CREATE | O_WRONLY | O_TRUNC, mode & 07777);
  if(fd < 0)
    return -1;

  left = size;
  while(left > 0) {
    int want = left > sizeof(buf) ? sizeof(buf) : (int)left;
    if(read_all(afd, buf, want) < 0) {
      close(fd);
      unlink(path);
      return -1;
    }
    if(write_all(fd, buf, want) < 0) {
      close(fd);
      unlink(path);
      return -1;
    }
    left -= (uint64_t)want;
  }

  if(close(fd) < 0) {
    unlink(path);
    return -1;
  }
  if(archive_skip_padding(afd, size) < 0) {
    unlink(path);
    return -1;
  }
  if(chmod(path, mode & 07777) < 0)
    return -1;
  return 0;
}

static int
extract_symlink(const char *path, const char *target)
{
  struct stat st;

  if(mkdir_parent(path) < 0)
    return -1;

  if(lstat(path, &st) == 0) {
    if(S_ISDIR(st.st_mode)) {
      errno = EISDIR;
      return -1;
    }
    if(unlink(path) < 0)
      return -1;
  } else if(errno != ENOENT) {
    return -1;
  }

  return symlink(target, path);
}

static int
extract_hardlink(const char *path, const char *target)
{
  struct stat st;

  if(mkdir_parent(path) < 0)
    return -1;

  if(lstat(path, &st) == 0) {
    if(S_ISDIR(st.st_mode)) {
      errno = EISDIR;
      return -1;
    }
    if(unlink(path) < 0)
      return -1;
  } else if(errno != ENOENT) {
    return -1;
  }

  return link(target, path);
}

static int
extract_fifo(const char *path, uint mode)
{
  struct stat st;

  if(mkdir_parent(path) < 0)
    return -1;

  if(lstat(path, &st) == 0) {
    if(S_ISDIR(st.st_mode)) {
      errno = EISDIR;
      return -1;
    }
    if(unlink(path) < 0)
      return -1;
  } else if(errno != ENOENT) {
    return -1;
  }

  if(mkfifo(path, mode & 07777) < 0)
    return -1;
  return chmod(path, mode & 07777);
}

static int
skip_payload(int afd, uint64_t size)
{
  uchar buf[256];
  uint64_t left;

  left = size;
  while(left > 0) {
    int want = left > sizeof(buf) ? sizeof(buf) : (int)left;
    if(read_all(afd, buf, want) < 0)
      return -1;
    left -= (uint64_t)want;
  }

  return archive_skip_padding(afd, size);
}

static int
read_symlink_target(const char *path, char **target_out)
{
  char buf[TAR_PATH_MAX + 1];
  int n;

  n = readlink(path, buf, sizeof(buf) - 1);
  if(n < 0)
    return -1;
  buf[n] = 0;
  return string_assign(target_out, buf);
}

static int
make_placeholder_name(const char *store, int is_dir, char *out, int out_sz)
{
  const char *base;
  int base_len;

  base = path_basename(store);
  if(*base == 0)
    base = is_dir ? "dir" : "file";

  base_len = strlen(base);
  if(is_dir) {
    if(base_len > out_sz - 2)
      base_len = out_sz - 2;
  } else if(base_len > out_sz - 1) {
    base_len = out_sz - 1;
  }

  if(base_len <= 0)
    return -1;

  memmove(out, base, base_len);
  if(is_dir && out[base_len - 1] != '/')
    out[base_len++] = '/';
  out[base_len] = 0;
  return 0;
}

static int
write_header_block(int afd,
                   const char *name,
                   char typeflag,
                   uint64_t size,
                   uint mode,
                   uint64_t uid,
                   uint64_t gid,
                   uint64_t mtime,
                   const char *linkname)
{
  struct tar_header h;

  memset(&h, 0, sizeof(h));
  if(split_name_prefix(name, h.name, sizeof(h.name), h.prefix, sizeof(h.prefix)) < 0) {
    errno = ENAMETOOLONG;
    return -1;
  }
  if(linkname && linkname[0]) {
    if(strlen(linkname) >= sizeof(h.linkname)) {
      errno = ENAMETOOLONG;
      return -1;
    }
    strcpy(h.linkname, linkname);
  }

  if(octal_write_checked(h.mode, sizeof(h.mode), mode & 07777) < 0 ||
     octal_write_checked(h.uid, sizeof(h.uid), uid) < 0 ||
     octal_write_checked(h.gid, sizeof(h.gid), gid) < 0 ||
     octal_write_checked(h.size, sizeof(h.size), size) < 0 ||
     octal_write_checked(h.mtime, sizeof(h.mtime), mtime) < 0)
    return -1;

  h.typeflag = typeflag;
  strcpy(h.magic, "ustar");
  h.version[0] = '0';
  h.version[1] = '0';
  tar_finalize_header(&h);
  return write_all(afd, &h, sizeof(h));
}

static int
write_pax_header(int afd, const char *store, const char *payload, uint payload_len)
{
  char name[TAR_PATH_MAX];
  const char *base;
  char dummy_name[100];
  char dummy_prefix[155];

  base = path_basename(store);
  if(*base == 0)
    base = "entry";
  if(join_path(name, sizeof(name), "PaxHeaders", base) < 0 ||
     split_name_prefix(name, dummy_name, sizeof(dummy_name), dummy_prefix, sizeof(dummy_prefix)) < 0)
    strcpy(name, "PaxHeaders/entry");

  if(write_header_block(afd, name, TAR_TYPE_XHDR, payload_len, 0644, 0, 0, 0, 0) < 0)
    return -1;
  if(write_all(afd, payload, payload_len) < 0)
    return -1;
  return archive_write_padding(afd, payload_len);
}

static int
tar_write_file_data(int afd, const char *path, uint64_t size)
{
  int fd;
  uchar buf[1024];
  uint64_t left;

  fd = open(path, O_RDONLY);
  if(fd < 0)
    return -1;

  left = size;
  while(left > 0) {
    int want = left > sizeof(buf) ? sizeof(buf) : (int)left;
    int n = read(fd, buf, want);
    if(n <= 0) {
      close(fd);
      return -1;
    }
    if(write_all(afd, buf, n) < 0) {
      close(fd);
      return -1;
    }
    left -= (uint64_t)n;
  }

  close(fd);
  return archive_write_padding(afd, size);
}

static int
tar_write_entry(int afd, const char *path, const char *store)
{
  struct stat st;
  char *link_target;
  char *pax_payload;
  uint pax_len;
  uint pax_cap;
  char header_name[TAR_PATH_MAX];
  uint64_t payload_size;
  char typeflag;
  int is_dir;
  int need_pax_name;
  int need_pax_link;
  int need_pax_size;
  const char *header_link;
  char dummy_name[100];
  char dummy_prefix[155];

  if(lstat(path, &st) < 0)
    return -1;

  link_target = 0;
  pax_payload = 0;
  pax_len = 0;
  pax_cap = 0;
  payload_size = 0;
  typeflag = TAR_TYPE_REG;
  is_dir = 0;
  header_link = 0;

  if(S_ISDIR(st.st_mode)) {
    typeflag = TAR_TYPE_DIR;
    payload_size = 0;
    is_dir = 1;
  } else if(S_ISLNK(st.st_mode)) {
    typeflag = TAR_TYPE_SYMLINK;
    payload_size = 0;
    if(read_symlink_target(path, &link_target) < 0)
      return -1;
    header_link = link_target;
  } else if(S_ISREG(st.st_mode)) {
    typeflag = TAR_TYPE_REG;
    payload_size = (uint64_t)st.st_size;
  } else if(S_ISFIFO(st.st_mode)) {
    typeflag = TAR_TYPE_FIFO;
    payload_size = 0;
  } else {
    dprintf(2, "tar: %s: unsupported file type\n", path);
    string_clear(&link_target);
    errno = EOPNOTSUPP;
    return -1;
  }

  if(is_dir) {
    if(strlen(store) >= sizeof(header_name) - 1) {
      string_clear(&link_target);
      errno = ENAMETOOLONG;
      return -1;
    }
    strcpy(header_name, store);
    if(header_name[0] && header_name[strlen(header_name) - 1] != '/')
      strcat(header_name, "/");
  } else {
    if(strlen(store) >= sizeof(header_name)) {
      string_clear(&link_target);
      errno = ENAMETOOLONG;
      return -1;
    }
    strcpy(header_name, store);
  }

  need_pax_name = split_name_prefix(header_name, dummy_name, sizeof(dummy_name), dummy_prefix, sizeof(dummy_prefix)) < 0;
  need_pax_link = header_link && strlen(header_link) >= sizeof(((struct tar_header*)0)->linkname);
  need_pax_size = typeflag == TAR_TYPE_REG && !octal_fits(sizeof(((struct tar_header*)0)->size), payload_size);

  if(need_pax_name) {
    if(append_pax_record(&pax_payload, &pax_len, &pax_cap, "path", header_name) < 0)
      goto fail;
    if(make_placeholder_name(header_name, is_dir, header_name, sizeof(header_name)) < 0)
      goto fail;
  }
  if(need_pax_link) {
    if(append_pax_record(&pax_payload, &pax_len, &pax_cap, "linkpath", header_link) < 0)
      goto fail;
    header_link = "";
  }
  if(need_pax_size) {
    if(append_pax_record_u64(&pax_payload, &pax_len, &pax_cap, "size", payload_size) < 0)
      goto fail;
  }

  if(pax_len) {
    if(write_pax_header(afd, store, pax_payload, pax_len) < 0)
      goto fail;
  }

  if(write_header_block(afd,
                        header_name,
                        typeflag,
                        need_pax_size ? 0 : payload_size,
                        st.st_mode & 07777,
                        (uint64_t)st.st_uid,
                        (uint64_t)st.st_gid,
                        (uint64_t)st.st_mtime,
                        header_link) < 0)
    goto fail;

  if(typeflag == TAR_TYPE_REG) {
    if(tar_write_file_data(afd, path, payload_size) < 0)
      goto fail;
  }

  string_clear(&link_target);
  string_clear(&pax_payload);
  return 0;

fail:
  string_clear(&link_target);
  string_clear(&pax_payload);
  return -1;
}

static int
tar_add_path(int afd, const char *path, const char *store)
{
  struct stat st;

  if(lstat(path, &st) < 0) {
    dprintf(2, "tar: %s: cannot stat\n", path);
    return -1;
  }

  if(S_ISDIR(st.st_mode)) {
    DIR *dp;
    struct dirent *de;

    if(tar_write_entry(afd, path, store) < 0) {
      dprintf(2, "tar: %s: failed to write directory header\n", path);
      return -1;
    }

    dp = opendir(path);
    if(dp == 0) {
      dprintf(2, "tar: %s: cannot open directory\n", path);
      return -1;
    }

    while((de = readdir(dp)) != 0) {
      char child_path[TAR_PATH_MAX];
      char child_store[TAR_PATH_MAX];

      if(strcmp(de->d_name, ".") == 0 || strcmp(de->d_name, "..") == 0)
        continue;

      if(join_path(child_path, sizeof(child_path), path, de->d_name) < 0 ||
         join_path(child_store, sizeof(child_store), store, de->d_name) < 0) {
        closedir(dp);
        dprintf(2, "tar: path too long under %s\n", path);
        errno = ENAMETOOLONG;
        return -1;
      }

      if(tar_add_path(afd, child_path, child_store) < 0) {
        closedir(dp);
        return -1;
      }
    }

    closedir(dp);
    return 0;
  }

  if(S_ISREG(st.st_mode) || S_ISLNK(st.st_mode) || S_ISFIFO(st.st_mode))
    return tar_write_entry(afd, path, store);

  dprintf(2, "tar: %s: unsupported file type\n", path);
  errno = EOPNOTSUPP;
  return -1;
}

static int
tar_create_archive(const struct tar_opts *opts, char **paths, int npaths)
{
  int afd;
  int i;
  uchar zero[TAR_BLOCK];

  afd = open(opts->archive, O_CREATE | O_WRONLY | O_TRUNC, 0666);
  if(afd < 0) {
    dprintf(2, "tar: cannot create %s\n", opts->archive);
    return -1;
  }

  for(i = 0; i < npaths; i++) {
    if(tar_add_path(afd, paths[i], paths[i]) < 0) {
      close(afd);
      return -1;
    }
  }

  memset(zero, 0, sizeof(zero));
  if(write_all(afd, zero, sizeof(zero)) < 0 ||
     write_all(afd, zero, sizeof(zero)) < 0) {
    close(afd);
    return -1;
  }

  close(afd);
  return 0;
}

static int
tar_create_archive_gzip(const struct tar_opts *opts, char **paths, int npaths)
{
  char tmp_path[64];
  int tfd;
  int i;
  int afd;
  int rc;

  strcpy(tmp_path, "/tmp/tar-create.XXXXXX");
  tfd = mkstemp(tmp_path);
  if(tfd < 0) {
    dprintf(2, "tar: unable to create temporary file\n");
    return -1;
  }

  afd = -1;
  rc = -1;

  for(i = 0; i < npaths; i++) {
    if(tar_add_path(tfd, paths[i], paths[i]) < 0)
      goto out;
  }

  {
    uchar zero[TAR_BLOCK];
    memset(zero, 0, sizeof(zero));
    if(write_all(tfd, zero, sizeof(zero)) < 0 ||
       write_all(tfd, zero, sizeof(zero)) < 0)
      goto out;
  }

  if(lseek(tfd, 0, SEEK_SET) < 0)
    goto out;

  afd = open(opts->archive, O_CREATE | O_WRONLY | O_TRUNC, 0666);
  if(afd < 0) {
    dprintf(2, "tar: cannot create %s\n", opts->archive);
    goto out;
  }

  if(aux_gzip_deflate_store_fd(tfd, afd) < 0) {
    dprintf(2, "tar: gzip write failed for %s\n", opts->archive);
    goto out;
  }

  rc = 0;

out:
  if(afd >= 0)
    close(afd);
  if(tfd >= 0)
    close(tfd);

  if(rc < 0)
    unlink(opts->archive);
  unlink(tmp_path);
  return rc;
}

static int
tar_create_archive_bzip2(const struct tar_opts *opts, char **paths, int npaths)
{
  char tmp_path[64];
  int tfd;
  int i;
  int afd;
  int rc;

  strcpy(tmp_path, "/tmp/tar-create.XXXXXX");
  tfd = mkstemp(tmp_path);
  if(tfd < 0) {
    dprintf(2, "tar: unable to create temporary file\n");
    return -1;
  }

  afd = -1;
  rc = -1;

  for(i = 0; i < npaths; i++) {
    if(tar_add_path(tfd, paths[i], paths[i]) < 0)
      goto out;
  }

  {
    uchar zero[TAR_BLOCK];
    memset(zero, 0, sizeof(zero));
    if(write_all(tfd, zero, sizeof(zero)) < 0 ||
       write_all(tfd, zero, sizeof(zero)) < 0)
      goto out;
  }

  if(lseek(tfd, 0, SEEK_SET) < 0)
    goto out;

  afd = open(opts->archive, O_CREATE | O_WRONLY | O_TRUNC, 0666);
  if(afd < 0) {
    dprintf(2, "tar: cannot create %s\n", opts->archive);
    goto out;
  }

  if(aux_bzip2_deflate_fd(tfd, afd) < 0) {
    dprintf(2, "tar: bzip2 write failed for %s\n", opts->archive);
    goto out;
  }

  rc = 0;

out:
  if(afd >= 0)
    close(afd);
  if(tfd >= 0)
    close(tfd);

  if(rc < 0)
    unlink(opts->archive);
  unlink(tmp_path);
  return rc;
}

static int
prepare_archive_reader(const struct tar_opts *opts, int *fd_out, char *tmp_path, int tmp_sz)
{
  int in_fd;

  in_fd = open(opts->archive, O_RDONLY);
  if(in_fd < 0) {
    dprintf(2, "tar: cannot open %s\n", opts->archive);
    return -1;
  }

  if(opts->gzip || aux_gzip_has_suffix(opts->archive)) {
    int tmp_fd;
    struct stat st;

    if(tmp_sz < 16) {
      close(in_fd);
      errno = EINVAL;
      return -1;
    }

    strcpy(tmp_path, "/tmp/tar.XXXXXX");
    tmp_fd = mkstemp(tmp_path);
    if(tmp_fd < 0) {
      close(in_fd);
      dprintf(2, "tar: unable to create temporary file\n");
      return -1;
    }

    if(aux_gzip_inflate_fd(in_fd, tmp_fd) < 0) {
      int err;

      err = errno;
      close(in_fd);
      close(tmp_fd);
      unlink(tmp_path);
      dprintf(2, "tar: %s: %s (errno=%d)\n", opts->archive, gzip_errno_detail(err), err);
      return -1;
    }

    close(in_fd);
    if(lseek(tmp_fd, 0, SEEK_SET) < 0) {
      close(tmp_fd);
      unlink(tmp_path);
      return -1;
    }

    memset(&st, 0, sizeof(st));
    if(fstat(tmp_fd, &st) < 0) {
      close(tmp_fd);
      unlink(tmp_path);
      dprintf(2, "tar: %s: cannot stat temporary archive\n", opts->archive);
      return -1;
    }
    if(st.st_size == 0) {
      close(tmp_fd);
      unlink(tmp_path);
      dprintf(2, "tar: %s: gzip decompressor produced empty payload\n", opts->archive);
      return -1;
    }

    *fd_out = tmp_fd;
    return 0;
  }

  if(opts->bzip2 || aux_bzip2_has_suffix(opts->archive)) {
    int tmp_fd;
    struct stat st;

    if(tmp_sz < 16) {
      close(in_fd);
      errno = EINVAL;
      return -1;
    }

    strcpy(tmp_path, "/tmp/tar.XXXXXX");
    tmp_fd = mkstemp(tmp_path);
    if(tmp_fd < 0) {
      close(in_fd);
      dprintf(2, "tar: unable to create temporary file\n");
      return -1;
    }

    if(aux_bzip2_inflate_fd(in_fd, tmp_fd) < 0) {
      int err;

      err = errno;
      close(in_fd);
      close(tmp_fd);
      unlink(tmp_path);
      dprintf(2, "tar: %s: %s (errno=%d)\n", opts->archive, bzip2_errno_detail(err), err);
      return -1;
    }

    close(in_fd);
    if(lseek(tmp_fd, 0, SEEK_SET) < 0) {
      close(tmp_fd);
      unlink(tmp_path);
      return -1;
    }
    if(fstat(tmp_fd, &st) < 0) {
      close(tmp_fd);
      unlink(tmp_path);
      dprintf(2, "tar: %s: cannot stat temporary archive\n", opts->archive);
      return -1;
    }
    if(st.st_size == 0) {
      close(tmp_fd);
      unlink(tmp_path);
      dprintf(2, "tar: %s: bzip2 decompressor produced empty payload\n", opts->archive);
      return -1;
    }

    *fd_out = tmp_fd;
    return 0;
  }

  tmp_path[0] = 0;
  *fd_out = in_fd;
  return 0;
}

static int
build_header_name(const struct tar_header *h, char **name_out)
{
  char buf[TAR_PATH_MAX];

  if(h->prefix[0]) {
    if(snprintf(buf, sizeof(buf), "%s/%s", h->prefix, h->name) >= sizeof(buf)) {
      errno = ENAMETOOLONG;
      return -1;
    }
  } else {
    if(snprintf(buf, sizeof(buf), "%s", h->name) >= sizeof(buf)) {
      errno = ENAMETOOLONG;
      return -1;
    }
  }
  return string_assign(name_out, buf);
}

static int
header_is_regular(const struct tar_header *h)
{
  return h->typeflag == TAR_TYPE_REG ||
         h->typeflag == TAR_TYPE_AREG ||
         h->typeflag == TAR_TYPE_CONTIG;
}

static int
header_is_directory(const struct tar_header *h, const char *name)
{
  int n;

  if(h->typeflag == TAR_TYPE_DIR)
    return 1;
  if(name == 0)
    return 0;
  n = strlen(name);
  if(header_is_regular(h) && n > 0 && name[n - 1] == '/')
    return 1;
  return 0;
}

static int
tar_read_archive(const struct tar_opts *opts)
{
  int afd;
  char tmp_path[64];
  int saw_block;
  struct tar_overrides overrides;

  tmp_path[0] = 0;
  memset(&overrides, 0, sizeof(overrides));

  if(prepare_archive_reader(opts, &afd, tmp_path, sizeof(tmp_path)) < 0)
    return -1;

  saw_block = 0;

  while(1) {
    struct tar_header h;
    uchar *hb;
    uint64_t size;
    uint64_t mode;
    uint got_sum;
    uint want_sum;
    char *header_name;
    char *member_name;
    const char *effective_linkname;
    char *extract_name;
    char *hardlink_target;
    int n;
    int off;

    header_name = 0;
    member_name = 0;
    effective_linkname = 0;
    extract_name = 0;
    hardlink_target = 0;

    n = read(afd, &h, sizeof(h));
    if(n == 0) {
      if(!saw_block) {
        dprintf(2, "tar: empty or invalid archive\n");
        close(afd);
        if(tmp_path[0])
          unlink(tmp_path);
        overrides_clear_all(&overrides);
        return -1;
      }
      break;
    }
    if(n < 0) {
      close(afd);
      if(tmp_path[0])
        unlink(tmp_path);
      overrides_clear_all(&overrides);
      return -1;
    }
    off = n;
    while(off < sizeof(h)) {
      int m = read(afd, ((uchar*)&h) + off, sizeof(h) - off);
      if(m <= 0) {
        dprintf(2, "tar: truncated archive\n");
        close(afd);
        if(tmp_path[0])
          unlink(tmp_path);
        overrides_clear_all(&overrides);
        return -1;
      }
      off += m;
    }

    saw_block = 1;
    hb = (uchar*)&h;
    if(is_all_zero(hb, TAR_BLOCK))
      break;

    got_sum = (uint)octal_parse(h.chksum, sizeof(h.chksum));
    {
      int i;
      for(i = 0; i < 8; i++)
        h.chksum[i] = ' ';
    }
    want_sum = (uint)tar_checksum(&h);
    if(got_sum != want_sum) {
      dprintf(2, "tar: checksum mismatch\n");
      close(afd);
      if(tmp_path[0])
        unlink(tmp_path);
      overrides_clear_all(&overrides);
      return -1;
    }

    if(build_header_name(&h, &header_name) < 0) {
      dprintf(2, "tar: header name too long\n");
      close(afd);
      if(tmp_path[0])
        unlink(tmp_path);
      overrides_clear_all(&overrides);
      return -1;
    }

    size = octal_parse(h.size, sizeof(h.size));
    mode = octal_parse(h.mode, sizeof(h.mode));

    if(h.typeflag == TAR_TYPE_XHDR || h.typeflag == TAR_TYPE_SOLARIS_XHDR) {
      overrides_clear_local(&overrides);
      if(pax_parse_payload(afd, size, &overrides.local) < 0) {
        dprintf(2, "tar: invalid pax extended header\n");
        string_clear(&header_name);
        close(afd);
        if(tmp_path[0])
          unlink(tmp_path);
        overrides_clear_all(&overrides);
        return -1;
      }
      string_clear(&header_name);
      continue;
    }

    if(h.typeflag == TAR_TYPE_GHDR) {
      if(pax_parse_payload(afd, size, &overrides.global) < 0) {
        dprintf(2, "tar: invalid global pax header\n");
        string_clear(&header_name);
        close(afd);
        if(tmp_path[0])
          unlink(tmp_path);
        overrides_clear_all(&overrides);
        return -1;
      }
      string_clear(&header_name);
      continue;
    }

    if(h.typeflag == TAR_TYPE_GNU_LONGNAME) {
      string_clear(&overrides.gnu_longname);
      if(read_payload_string(afd, size, &overrides.gnu_longname) < 0) {
        dprintf(2, "tar: invalid GNU longname header\n");
        string_clear(&header_name);
        close(afd);
        if(tmp_path[0])
          unlink(tmp_path);
        overrides_clear_all(&overrides);
        return -1;
      }
      string_clear(&header_name);
      continue;
    }

    if(h.typeflag == TAR_TYPE_GNU_LONGLINK) {
      string_clear(&overrides.gnu_longlink);
      if(read_payload_string(afd, size, &overrides.gnu_longlink) < 0) {
        dprintf(2, "tar: invalid GNU longlink header\n");
        string_clear(&header_name);
        close(afd);
        if(tmp_path[0])
          unlink(tmp_path);
        overrides_clear_all(&overrides);
        return -1;
      }
      string_clear(&header_name);
      continue;
    }

    if(overrides.gnu_longname)
      member_name = overrides.gnu_longname;
    else if(overrides.local.path)
      member_name = overrides.local.path;
    else if(overrides.global.path)
      member_name = overrides.global.path;
    else
      member_name = header_name;

    if(overrides.gnu_longlink)
      effective_linkname = overrides.gnu_longlink;
    else if(overrides.local.linkpath)
      effective_linkname = overrides.local.linkpath;
    else if(overrides.global.linkpath)
      effective_linkname = overrides.global.linkpath;
    else
      effective_linkname = h.linkname;

    if(overrides.local.have_size)
      size = overrides.local.size;
    else if(overrides.global.have_size)
      size = overrides.global.size;

    if(sanitize_extract_path(member_name, &extract_name) < 0) {
      dprintf(2, "tar: refusing unsafe path %s\n", member_name);
      string_clear(&header_name);
      string_clear(&extract_name);
      close(afd);
      if(tmp_path[0])
        unlink(tmp_path);
      overrides_clear_all(&overrides);
      return -1;
    }

    if(h.typeflag == TAR_TYPE_GNU_SPARSE) {
      dprintf(2, "tar: %s: GNU sparse files are not supported\n", extract_name[0] ? extract_name : member_name);
      string_clear(&header_name);
      string_clear(&extract_name);
      close(afd);
      if(tmp_path[0])
        unlink(tmp_path);
      overrides_clear_all(&overrides);
      return -1;
    }

    if(h.typeflag == TAR_TYPE_GNU_DUMPDIR ||
       h.typeflag == TAR_TYPE_GNU_MULTIVOL ||
       h.typeflag == TAR_TYPE_GNU_NAMES ||
       h.typeflag == TAR_TYPE_GNU_VOLHDR) {
      if(skip_payload(afd, size) < 0) {
        string_clear(&header_name);
        string_clear(&extract_name);
        close(afd);
        if(tmp_path[0])
          unlink(tmp_path);
        overrides_clear_all(&overrides);
        return -1;
      }
      overrides_clear_local(&overrides);
      string_clear(&header_name);
      string_clear(&extract_name);
      continue;
    }

    if(opts->mode_list) {
      if(extract_name[0])
        dprintf(1, "%s\n", extract_name);
      if(skip_payload(afd, size) < 0) {
        string_clear(&header_name);
        string_clear(&extract_name);
        close(afd);
        if(tmp_path[0])
          unlink(tmp_path);
        overrides_clear_all(&overrides);
        return -1;
      }
      overrides_clear_local(&overrides);
      string_clear(&header_name);
      string_clear(&extract_name);
      continue;
    }

    if(extract_name[0] == 0) {
      if(skip_payload(afd, size) < 0) {
        string_clear(&header_name);
        string_clear(&extract_name);
        close(afd);
        if(tmp_path[0])
          unlink(tmp_path);
        overrides_clear_all(&overrides);
        return -1;
      }
      overrides_clear_local(&overrides);
      string_clear(&header_name);
      string_clear(&extract_name);
      continue;
    }

    if(opts->mode_extract) {
      if(header_is_directory(&h, member_name)) {
        if(extract_directory(extract_name, (uint)mode) < 0) {
          dprintf(2, "tar: %s: mkdir failed (errno=%d)\n", extract_name, errno);
          string_clear(&header_name);
          string_clear(&extract_name);
          close(afd);
          if(tmp_path[0])
            unlink(tmp_path);
          overrides_clear_all(&overrides);
          return -1;
        }
        if(skip_payload(afd, size) < 0) {
          string_clear(&header_name);
          string_clear(&extract_name);
          close(afd);
          if(tmp_path[0])
            unlink(tmp_path);
          overrides_clear_all(&overrides);
          return -1;
        }
      } else if(h.typeflag == TAR_TYPE_SYMLINK) {
        if(extract_symlink(extract_name, effective_linkname) < 0) {
          dprintf(2, "tar: %s: symlink failed (errno=%d)\n", extract_name, errno);
          string_clear(&header_name);
          string_clear(&extract_name);
          close(afd);
          if(tmp_path[0])
            unlink(tmp_path);
          overrides_clear_all(&overrides);
          return -1;
        }
        if(skip_payload(afd, size) < 0) {
          string_clear(&header_name);
          string_clear(&extract_name);
          close(afd);
          if(tmp_path[0])
            unlink(tmp_path);
          overrides_clear_all(&overrides);
          return -1;
        }
      } else if(h.typeflag == TAR_TYPE_HARDLINK) {
        if(sanitize_extract_path(effective_linkname, &hardlink_target) < 0) {
          dprintf(2, "tar: %s: refusing unsafe hard link target %s\n", extract_name, effective_linkname);
          string_clear(&header_name);
          string_clear(&extract_name);
          close(afd);
          if(tmp_path[0])
            unlink(tmp_path);
          overrides_clear_all(&overrides);
          return -1;
        }
        if(extract_hardlink(extract_name, hardlink_target) < 0) {
          dprintf(2, "tar: %s: hard link failed (errno=%d)\n", extract_name, errno);
          string_clear(&header_name);
          string_clear(&extract_name);
          string_clear(&hardlink_target);
          close(afd);
          if(tmp_path[0])
            unlink(tmp_path);
          overrides_clear_all(&overrides);
          return -1;
        }
        if(skip_payload(afd, size) < 0) {
          string_clear(&header_name);
          string_clear(&extract_name);
          string_clear(&hardlink_target);
          close(afd);
          if(tmp_path[0])
            unlink(tmp_path);
          overrides_clear_all(&overrides);
          return -1;
        }
      } else if(h.typeflag == TAR_TYPE_FIFO) {
        if(extract_fifo(extract_name, (uint)mode) < 0) {
          dprintf(2, "tar: %s: fifo create failed (errno=%d)\n", extract_name, errno);
          string_clear(&header_name);
          string_clear(&extract_name);
          close(afd);
          if(tmp_path[0])
            unlink(tmp_path);
          overrides_clear_all(&overrides);
          return -1;
        }
        if(skip_payload(afd, size) < 0) {
          string_clear(&header_name);
          string_clear(&extract_name);
          close(afd);
          if(tmp_path[0])
            unlink(tmp_path);
          overrides_clear_all(&overrides);
          return -1;
        }
      } else if(h.typeflag == TAR_TYPE_CHAR || h.typeflag == TAR_TYPE_BLOCK) {
        dprintf(2, "tar: %s: special device extraction is not supported\n", extract_name);
        string_clear(&header_name);
        string_clear(&extract_name);
        close(afd);
        if(tmp_path[0])
          unlink(tmp_path);
        overrides_clear_all(&overrides);
        return -1;
      } else if(header_is_regular(&h) ||
                (h.typeflag != TAR_TYPE_DIR && h.typeflag != TAR_TYPE_HARDLINK &&
                 h.typeflag != TAR_TYPE_SYMLINK && h.typeflag != TAR_TYPE_FIFO)) {
        if(extract_regular(afd, extract_name, size, (uint)mode) < 0) {
          dprintf(2, "tar: %s: extract failed (errno=%d, size=%llu, type=%c)\n",
                  extract_name,
                  errno,
                  (unsigned long long)size,
                  h.typeflag ? h.typeflag : '0');
          string_clear(&header_name);
          string_clear(&extract_name);
          close(afd);
          if(tmp_path[0])
            unlink(tmp_path);
          overrides_clear_all(&overrides);
          return -1;
        }
      }

      if(opts->verbose)
        dprintf(1, "%s\n", extract_name);
    }

    overrides_clear_local(&overrides);
    string_clear(&header_name);
    string_clear(&extract_name);
    string_clear(&hardlink_target);
  }

  close(afd);
  if(tmp_path[0])
    unlink(tmp_path);
  overrides_clear_all(&overrides);
  return 0;
}

int
main(int argc, char *argv[])
{
  struct tar_opts opts;
  char *arg;
  int i;
  int files_start;
  int used_oldstyle;

  memset(&opts, 0, sizeof(opts));
  files_start = -1;
  used_oldstyle = 0;

  for(i = 1; i < argc; i++) {
    int oldstyle;
    int j;
    int start;

    if(strcmp(argv[i], "--") == 0) {
      i++;
      break;
    }

    oldstyle = (i == 1 && argv[i][0] != '-' && is_oldstyle_flag_word(argv[i]));
    if(!oldstyle && (argv[i][0] != '-' || argv[i][1] == 0))
      break;

    if(oldstyle)
      used_oldstyle = 1;

    arg = argv[i];
    start = oldstyle ? 0 : 1;

    for(j = start; arg[j]; j++) {
      switch(arg[j]) {
      case 'c': opts.mode_create = 1; break;
      case 't': opts.mode_list = 1; break;
      case 'x': opts.mode_extract = 1; break;
      case 'z':
      case 'g': opts.gzip = 1; break;
      case 'j': opts.bzip2 = 1; break;
      case 'v': opts.verbose = 1; break;
      case 'f':
        if(oldstyle) {
          if(i + 1 >= argc)
            usage();
          opts.archive = argv[++i];
        } else if(arg[j + 1]) {
          opts.archive = arg + j + 1;
          j = strlen(arg) - 1;
        } else {
          if(i + 1 >= argc)
            usage();
          opts.archive = argv[++i];
          j = strlen(arg) - 1;
        }
        break;
      default:
        usage();
      }
    }
  }

  files_start = i;

  if(used_oldstyle && opts.archive == 0 && files_start < argc) {
    opts.archive = argv[files_start];
    files_start++;
  }

  if((opts.mode_create + opts.mode_extract + opts.mode_list) != 1)
    usage();
  if(opts.archive == 0)
    usage();
  if(opts.gzip && opts.bzip2) {
    dprintf(2, "tar: choose only one compression mode (-z/-g or -j)\n");
    return 1;
  }

  if(opts.mode_create) {
    if(files_start >= argc) {
      dprintf(2, "tar: create mode requires at least one path\n");
      return 1;
    }
    if(opts.gzip) {
      if(tar_create_archive_gzip(&opts, argv + files_start, argc - files_start) < 0)
        return 1;
    } else if(opts.bzip2) {
      if(tar_create_archive_bzip2(&opts, argv + files_start, argc - files_start) < 0)
        return 1;
    } else {
      if(tar_create_archive(&opts, argv + files_start, argc - files_start) < 0)
        return 1;
    }
    return 0;
  }

  if(tar_read_archive(&opts) < 0)
    return 1;

  return 0;
}
