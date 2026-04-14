#include "types.h"
#include "auxv6/user.h"
#include "auxv6/gzip.h"
#include "auxv6/bzip2.h"
#include "dirent.h"
#include "stat.h"
#include "fcntl.h"
#include "unistd.h"
#include "errno.h"
#include "stdlib.h"
#include "string.h"
#include "stdio.h"

#define TAR_BLOCK 512

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

static void
usage(void)
{
  dprintf(2,
          "usage: tar -c|-t|-x [-v] [-z] [-j] -f archive [path ...]\n");
  exit(1);
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

static void
octal_write(char *dst, int width, uint64_t v)
{
  int i;

  for(i = 0; i < width; i++)
    dst[i] = '0';

  dst[width - 1] = '\0';
  for(i = width - 2; i >= 0 && v; i--) {
    dst[i] = '0' + (v & 7U);
    v >>= 3;
  }
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
  octal_write(h->chksum, sizeof(h->chksum), (uint64_t)sum);
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

static int
path_is_safe(const char *path)
{
  int i;

  if(path[0] == '/')
    return 0;

  for(i = 0; path[i]; i++) {
    if(path[i] == '.' && (i == 0 || path[i - 1] == '/')) {
      if(path[i + 1] == '/' || path[i + 1] == 0)
        continue;
      if(path[i + 1] == '.' && (path[i + 2] == '/' || path[i + 2] == 0))
        return 0;
    }
  }

  return 1;
}

static int
mkdir_p(const char *path)
{
  char buf[256];
  int i;
  int n;

  n = strlen(path);
  if(n <= 0)
    return 0;
  if(n >= (int)sizeof(buf))
    return -1;

  memmove(buf, path, n + 1);
  for(i = 1; i < n; i++) {
    if(buf[i] != '/')
      continue;
    buf[i] = 0;
    if(buf[0] && mkdir(buf) < 0 && errno != EEXIST)
      return -1;
    buf[i] = '/';
  }

  if(mkdir(buf) < 0 && errno != EEXIST)
    return -1;
  return 0;
}

static int
mkdir_parent(const char *path)
{
  char buf[256];
  int i;

  if(strlen(path) >= sizeof(buf))
    return -1;
  strcpy(buf, path);

  for(i = strlen(buf) - 1; i >= 0; i--) {
    if(buf[i] == '/') {
      if(i == 0)
        return 0;
      buf[i] = 0;
      return mkdir_p(buf);
    }
  }

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
  struct tar_header h;

  if(lstat(path, &st) < 0)
    return -1;

  memset(&h, 0, sizeof(h));
  if(split_name_prefix(store, h.name, sizeof(h.name), h.prefix, sizeof(h.prefix)) < 0)
    return -1;

  octal_write(h.mode, sizeof(h.mode), (uint64_t)(st.st_mode & 07777));
  octal_write(h.uid, sizeof(h.uid), (uint64_t)(ushort)st.st_uid);
  octal_write(h.gid, sizeof(h.gid), (uint64_t)(ushort)st.st_gid);
  octal_write(h.mtime, sizeof(h.mtime), (uint64_t)(uint)st.st_mtime);
  strcpy(h.magic, "ustar");
  h.version[0] = '0';
  h.version[1] = '0';

  if(st.st_type == T_DIR) {
    h.typeflag = '5';
    octal_write(h.size, sizeof(h.size), 0);
  } else if(st.st_type == T_SYMLINK) {
    char target[100];
    int n;

    h.typeflag = '2';
    octal_write(h.size, sizeof(h.size), 0);
    n = readlink(path, target, sizeof(target) - 1);
    if(n < 0)
      return -1;
    target[n] = 0;
    if(strlen(target) >= sizeof(h.linkname))
      return -1;
    strcpy(h.linkname, target);
  } else {
    h.typeflag = '0';
    octal_write(h.size, sizeof(h.size), st.st_size);
  }

  tar_finalize_header(&h);
  if(write_all(afd, &h, sizeof(h)) < 0)
    return -1;

  if(st.st_type == T_FILE)
    return tar_write_file_data(afd, path, st.st_size);

  return 0;
}

static int
tar_add_path(int afd, const char *path, const char *store)
{
  struct stat st;

  if(lstat(path, &st) < 0) {
    dprintf(2, "tar: %s: cannot stat\n", path);
    return -1;
  }

  if(st.st_type == T_DIR) {
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
      char child_path[256];
      char child_store[256];

      if(strcmp(de->d_name, ".") == 0 || strcmp(de->d_name, "..") == 0)
        continue;

      if(join_path(child_path, sizeof(child_path), path, de->d_name) < 0 ||
         join_path(child_store, sizeof(child_store), store, de->d_name) < 0) {
        closedir(dp);
        dprintf(2, "tar: path too long under %s\n", path);
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

  if(st.st_type == T_FILE || st.st_type == T_SYMLINK)
    return tar_write_entry(afd, path, store);

  return 0;
}

static int
tar_create_archive(const struct tar_opts *opts, char **paths, int npaths)
{
  int afd;
  int i;
  uchar zero[TAR_BLOCK];

  afd = open(opts->archive, O_CREATE | O_WRONLY | O_TRUNC);
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
    if(tar_add_path(tfd, paths[i], paths[i]) < 0) {
      goto out;
    }
  }

  {
    uchar zero[TAR_BLOCK];
    memset(zero, 0, sizeof(zero));
    if(write_all(tfd, zero, sizeof(zero)) < 0 ||
       write_all(tfd, zero, sizeof(zero)) < 0) {
      goto out;
    }
  }

  if(lseek(tfd, 0, SEEK_SET) < 0) {
    goto out;
  }

  afd = open(opts->archive, O_CREATE | O_WRONLY | O_TRUNC);
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

    if(tmp_sz < 16) {
      close(in_fd);
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
      close(in_fd);
      close(tmp_fd);
      unlink(tmp_path);
      dprintf(2, "tar: %s: invalid or unsupported gzip archive\n", opts->archive);
      return -1;
    }

    close(in_fd);
    if(lseek(tmp_fd, 0, SEEK_SET) < 0) {
      close(tmp_fd);
      unlink(tmp_path);
      return -1;
    }

    *fd_out = tmp_fd;
    return 0;
  }

  if(opts->bzip2 || aux_bzip2_has_suffix(opts->archive)) {
    int tmp_fd;

    if(tmp_sz < 16) {
      close(in_fd);
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
      close(in_fd);
      close(tmp_fd);
      unlink(tmp_path);
      dprintf(2, "tar: %s: invalid or unsupported bzip2 archive\n", opts->archive);
      return -1;
    }

    close(in_fd);
    if(lseek(tmp_fd, 0, SEEK_SET) < 0) {
      close(tmp_fd);
      unlink(tmp_path);
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
extract_regular(int afd, const char *path, uint64_t size)
{
  int fd;
  uchar buf[1024];
  uint64_t left;

  if(mkdir_parent(path) < 0)
    return -1;

  fd = open(path, O_CREATE | O_WRONLY | O_TRUNC);
  if(fd < 0)
    return -1;

  left = size;
  while(left > 0) {
    int want = left > sizeof(buf) ? sizeof(buf) : (int)left;
    if(read_all(afd, buf, want) < 0) {
      close(fd);
      return -1;
    }
    if(write_all(fd, buf, want) < 0) {
      close(fd);
      return -1;
    }
    left -= (uint64_t)want;
  }

  close(fd);
  return archive_skip_padding(afd, size);
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
tar_read_archive(const struct tar_opts *opts)
{
  int afd;
  char tmp_path[64];

  if(prepare_archive_reader(opts, &afd, tmp_path, sizeof(tmp_path)) < 0)
    return -1;

  while(1) {
    struct tar_header h;
    uchar *hb;
    uint64_t size;
    uint got_sum;
    uint want_sum;
    char name[256];

    if(read_all(afd, &h, sizeof(h)) < 0)
      break;

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
      return -1;
    }

    if(h.prefix[0])
      snprintf(name, sizeof(name), "%s/%s", h.prefix, h.name);
    else
      snprintf(name, sizeof(name), "%s", h.name);

    size = octal_parse(h.size, sizeof(h.size));

    if(!path_is_safe(name)) {
      dprintf(2, "tar: refusing unsafe path %s\n", name);
      close(afd);
      if(tmp_path[0])
        unlink(tmp_path);
      return -1;
    }

    if(opts->mode_list) {
      dprintf(1, "%s\n", name);
      if(skip_payload(afd, size) < 0) {
        close(afd);
        if(tmp_path[0])
          unlink(tmp_path);
        return -1;
      }
      continue;
    }

    if(opts->mode_extract) {
      if(h.typeflag == '5') {
        if(mkdir_p(name) < 0) {
          dprintf(2, "tar: %s: mkdir failed\n", name);
          close(afd);
          if(tmp_path[0])
            unlink(tmp_path);
          return -1;
        }
        if(skip_payload(afd, size) < 0) {
          close(afd);
          if(tmp_path[0])
            unlink(tmp_path);
          return -1;
        }
      } else if(h.typeflag == '2') {
        if(mkdir_parent(name) < 0) {
          close(afd);
          if(tmp_path[0])
            unlink(tmp_path);
          return -1;
        }
        unlink(name);
        if(symlink(h.linkname, name) < 0) {
          dprintf(2, "tar: %s: symlink failed\n", name);
          close(afd);
          if(tmp_path[0])
            unlink(tmp_path);
          return -1;
        }
        if(skip_payload(afd, size) < 0) {
          close(afd);
          if(tmp_path[0])
            unlink(tmp_path);
          return -1;
        }
      } else if(h.typeflag == '1') {
        if(mkdir_parent(name) < 0) {
          close(afd);
          if(tmp_path[0])
            unlink(tmp_path);
          return -1;
        }
        unlink(name);
        if(link(h.linkname, name) < 0) {
          dprintf(2, "tar: %s: hard link failed\n", name);
          close(afd);
          if(tmp_path[0])
            unlink(tmp_path);
          return -1;
        }
        if(skip_payload(afd, size) < 0) {
          close(afd);
          if(tmp_path[0])
            unlink(tmp_path);
          return -1;
        }
      } else {
        if(extract_regular(afd, name, size) < 0) {
          dprintf(2, "tar: %s: extract failed\n", name);
          close(afd);
          if(tmp_path[0])
            unlink(tmp_path);
          return -1;
        }
      }

      if(opts->verbose)
        dprintf(1, "%s\n", name);
    }
  }

  close(afd);
  if(tmp_path[0])
    unlink(tmp_path);
  return 0;
}

int
main(int argc, char *argv[])
{
  struct tar_opts opts;
  char *arg;
  int i;
  int files_start;

  memset(&opts, 0, sizeof(opts));
  files_start = -1;

  for(i = 1; i < argc; i++) {
    if(argv[i][0] != '-' || argv[i][1] == 0) {
      files_start = i;
      break;
    }

    if(strcmp(argv[i], "--") == 0) {
      files_start = i + 1;
      break;
    }

    {
      int j;

      arg = argv[i];
      for(j = 1; arg[j]; j++) {
        switch(arg[j]) {
        case 'c': opts.mode_create = 1; break;
        case 't': opts.mode_list = 1; break;
        case 'x': opts.mode_extract = 1; break;
        case 'z': opts.gzip = 1; break;
        case 'j': opts.bzip2 = 1; break;
        case 'v': opts.verbose = 1; break;
        case 'f':
          if(arg[j + 1]) {
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
  }

  if(files_start < 0)
    files_start = argc;

  if((opts.mode_create + opts.mode_extract + opts.mode_list) != 1)
    usage();
  if(opts.archive == 0)
    usage();

  if(opts.mode_create) {
    if(files_start >= argc) {
      dprintf(2, "tar: create mode requires at least one path\n");
      return 1;
    }
    if(opts.gzip) {
      if(tar_create_archive_gzip(&opts, argv + files_start, argc - files_start) < 0)
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
