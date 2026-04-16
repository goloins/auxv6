#include "types.h"
#include "auxv6/user.h"
#include "fcntl.h"
#include "unistd.h"
#include "sys/stat.h"
#include "string.h"
#include "stdlib.h"
#include "stdio.h"
#include "errno.h"
#include "time.h"

/* AR archive format support - POSIX ustar-like format */

#define AR_MAGIC "!<arch>\n"
#define AR_MAGIC_LEN 8
#define AR_HEADER_LEN 60

struct ar_header {
  char ar_name[16];        /* Member filename */
  char ar_date[12];        /* Modification time (decimal) */
  char ar_uid[6];          /* Owner UID (decimal) */
  char ar_gid[6];          /* Owner GID (decimal) */
  char ar_mode[8];         /* File permissions (octal) */
  char ar_size[10];        /* File size in bytes (decimal) */
  char ar_magic[2];        /* Trailing magic: "`\n" */
};

static void
usage(void)
{
  write(2, "usage: ar [-t] [-x] [-r] [-d] [-p] [-c] [-v] archive [member ...]\n", 70);
  write(2, "       ar -q archive file ...\n", 30);
  exit(1);
}

static uint
ar_parse_decimal(const char *src, int width)
{
  uint v = 0;
  int i;

  for(i = 0; i < width && src[i] >= '0' && src[i] <= '9'; i++)
    v = v * 10 + (uint)(src[i] - '0');
  return v;
}

static void
ar_format_decimal(char *dst, int width, uint v)
{
  int i;
  
  for(i = width - 1; i >= 0; i--) {
    dst[i] = '0' + (v % 10);
    v /= 10;
  }
}

static void
ar_trim_name(char *name, int name_sz)
{
  int i;
  
  for(i = 0; i < name_sz && name[i] && name[i] != ' '; i++)
    ;
  if(i > 0 && name[i-1] == '/')
    i--;
  name[i] = 0;
}

static int
ar_list(const char *archive, int verbose)
{
  int fd;
  char magic[AR_MAGIC_LEN];
  struct ar_header hdr;
  char name[32];
  uint size;

  fd = open(archive, O_RDONLY);
  if(fd < 0) {
    dprintf(2, "ar: %s: cannot open\n", archive);
    return -1;
  }

  if(read(fd, magic, AR_MAGIC_LEN) != AR_MAGIC_LEN ||
     strncmp(magic, AR_MAGIC, AR_MAGIC_LEN) != 0) {
    dprintf(2, "ar: %s: not an ar archive\n", archive);
    close(fd);
    return -1;
  }

  while(1) {
    int n = read(fd, &hdr, sizeof(hdr));
    if(n == 0)
      break;
    if(n != sizeof(hdr)) {
      dprintf(2, "ar: %s: corrupted archive header\n", archive);
      close(fd);
      return -1;
    }

    if(hdr.ar_magic[0] != '`' || hdr.ar_magic[1] != '\n') {
      dprintf(2, "ar: %s: corrupted member magic\n", archive);
      close(fd);
      return -1;
    }

    memmove(name, hdr.ar_name, sizeof(hdr.ar_name));
    ar_trim_name(name, sizeof(hdr.ar_name));
    size = ar_parse_decimal(hdr.ar_size, sizeof(hdr.ar_size));

    if(verbose) {
      uint mode = ar_parse_decimal(hdr.ar_mode, sizeof(hdr.ar_mode));
      dprintf(1, "%o %u\t%s\n", mode, size, name);
    } else {
      dprintf(1, "%s\n", name);
    }

    /* Seek past member data (padded to even byte boundary) */
    if(size % 2)
      size++;
    if(lseek(fd, (off_t)size, SEEK_CUR) < 0) {
      dprintf(2, "ar: lseek failed\n");
      close(fd);
      return -1;
    }
  }

  close(fd);
  return 0;
}

static int
ar_extract(const char *archive, char **members, int nmembers, int verbose)
{
  int fd;
  char magic[AR_MAGIC_LEN];
  struct ar_header hdr;
  char name[32];
  uint size;
  int i;
  int want_all;
  int extracted;

  want_all = (nmembers == 0);

  fd = open(archive, O_RDONLY);
  if(fd < 0) {
    dprintf(2, "ar: %s: cannot open\n", archive);
    return -1;
  }

  if(read(fd, magic, AR_MAGIC_LEN) != AR_MAGIC_LEN ||
     strncmp(magic, AR_MAGIC, AR_MAGIC_LEN) != 0) {
    dprintf(2, "ar: %s: not an ar archive\n", archive);
    close(fd);
    return -1;
  }

  while(1) {
    int n = read(fd, &hdr, sizeof(hdr));
    if(n == 0)
      break;
    if(n != sizeof(hdr)) {
      dprintf(2, "ar: %s: corrupted archive header\n", archive);
      close(fd);
      return -1;
    }

    if(hdr.ar_magic[0] != '`' || hdr.ar_magic[1] != '\n') {
      dprintf(2, "ar: %s: corrupted member magic\n", archive);
      close(fd);
      return -1;
    }

    memmove(name, hdr.ar_name, sizeof(hdr.ar_name));
    ar_trim_name(name, sizeof(hdr.ar_name));
    size = ar_parse_decimal(hdr.ar_size, sizeof(hdr.ar_size));

    extracted = want_all;
    if(!want_all) {
      for(i = 0; i < nmembers; i++) {
        if(strcmp(name, members[i]) == 0) {
          extracted = 1;
          break;
        }
      }
    }

    if(extracted) {
      int out_fd;
      uint left;
      uchar buf[1024];

      out_fd = open(name, O_CREATE | O_WRONLY | O_TRUNC);
      if(out_fd < 0) {
        dprintf(2, "ar: %s: cannot create\n", name);
        close(fd);
        return -1;
      }

      left = size;
      while(left > 0) {
        int want = (left > sizeof(buf)) ? sizeof(buf) : (int)left;
        n = read(fd, buf, want);
        if(n <= 0) {
          dprintf(2, "ar: %s: read failed\n", archive);
          close(out_fd);
          close(fd);
          return -1;
        }
        if(write(out_fd, buf, n) != n) {
          dprintf(2, "ar: %s: write failed\n", name);
          close(out_fd);
          close(fd);
          return -1;
        }
        left -= (uint)n;
      }
      close(out_fd);

      if(verbose)
        dprintf(1, "x - %s\n", name);
    }

    /* Seek past member data (padded to even byte boundary) */
    if(size % 2)
      size++;
    if(lseek(fd, (off_t)size, SEEK_CUR) < 0) {
      dprintf(2, "ar: lseek failed\n");
      close(fd);
      return -1;
    }
  }

  close(fd);
  return 0;
}

static int
ar_append(const char *archive, char **files, int nfiles, int create, int verbose)
{
  int fd;
  int i;

  /* Open or create archive */
  fd = open(archive, O_WRONLY | O_CREAT);
  if(fd < 0) {
    dprintf(2, "ar: %s: cannot open\n", archive);
    return -1;
  }

  /* Seek to end for append mode */
  if(!create && lseek(fd, 0, SEEK_END) < 0) {
    dprintf(2, "ar: lseek failed\n");
    close(fd);
    return -1;
  }

  /* Write archive magic if creating */
  if(create) {
    if(write(fd, AR_MAGIC, AR_MAGIC_LEN) != AR_MAGIC_LEN) {
      dprintf(2, "ar: write failed\n");
      close(fd);
      return -1;
    }
  }

  /* Add each file */
  for(i = 0; i < nfiles; i++) {
    int in_fd;
    struct stat st;
    struct ar_header hdr;
    uchar buf[1024];
    int n;

    in_fd = open(files[i], O_RDONLY);
    if(in_fd < 0) {
      dprintf(2, "ar: %s: cannot open\n", files[i]);
      close(fd);
      return -1;
    }

    if(fstat(in_fd, &st) < 0) {
      dprintf(2, "ar: %s: fstat failed\n", files[i]);
      close(in_fd);
      close(fd);
      return -1;
    }

    /* Build member header */
    memset(&hdr, ' ', sizeof(hdr));
    snprintf(hdr.ar_name, sizeof(hdr.ar_name), "%-15s", files[i]);
    ar_format_decimal(hdr.ar_date, sizeof(hdr.ar_date), (uint)st.st_mtime);
    ar_format_decimal(hdr.ar_uid, sizeof(hdr.ar_uid), (uint)(ushort)st.st_uid);
    ar_format_decimal(hdr.ar_gid, sizeof(hdr.ar_gid), (uint)(ushort)st.st_gid);
    ar_format_decimal(hdr.ar_mode, sizeof(hdr.ar_mode), (uint)(st.st_mode & 07777));
    ar_format_decimal(hdr.ar_size, sizeof(hdr.ar_size), (uint)st.st_size);
    hdr.ar_magic[0] = '`';
    hdr.ar_magic[1] = '\n';

    if(write(fd, &hdr, sizeof(hdr)) != sizeof(hdr)) {
      dprintf(2, "ar: write failed\n");
      close(in_fd);
      close(fd);
      return -1;
    }

    /* Copy file content */
    while((n = read(in_fd, buf, sizeof(buf))) > 0) {
      if(write(fd, buf, n) != n) {
        dprintf(2, "ar: write failed\n");
        close(in_fd);
        close(fd);
        return -1;
      }
    }

    /* Pad to even boundary if needed */
    if(st.st_size % 2) {
      uchar pad = '\n';
      if(write(fd, &pad, 1) != 1) {
        dprintf(2, "ar: write failed\n");
        close(in_fd);
        close(fd);
        return -1;
      }
    }

    close(in_fd);
    if(verbose)
      dprintf(1, "a - %s\n", files[i]);
  }

  close(fd);
  return 0;
}

int
main(int argc, char *argv[])
{
  int mode_list = 0;
  int mode_extract = 0;
  int mode_append = 0;
  int create = 0;
  int verbose = 0;
  int i;
  const char *archive;

  if(argc < 2)
    usage();

  if(argv[1][0] == '-')
    argv[1]++;  /* Skip leading dash if present */

  /* Parse command letters */
  for(i = 0; argv[1][i]; i++) {
    switch(argv[1][i]) {
    case 't': mode_list = 1; break;
    case 'x': mode_extract = 1; break;
    case 'r': mode_append = 1; break;
    case 'q': mode_append = 1; break;
    case 'c': create = 1; break;
    case 'v': verbose = 1; break;
    default:
      usage();
    }
  }

  if(argc < 3)
    usage();

  archive = argv[2];

  if(mode_list) {
    if(ar_list(archive, verbose) < 0)
      return 1;
    return 0;
  }

  if(mode_extract) {
    if(ar_extract(archive, argv + 3, argc - 3, verbose) < 0)
      return 1;
    return 0;
  }

  if(mode_append) {
    if(argc < 4)
      usage();
    if(ar_append(archive, argv + 3, argc - 3, create, verbose) < 0)
      return 1;
    return 0;
  }

  usage();
  return 1;
}
