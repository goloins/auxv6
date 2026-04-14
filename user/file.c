#include "types.h"
#include "fcntl.h"
#include "sys/stat.h"
#include "auxv6/user.h"

#define PROBE_SIZE 4096

static int
has_prefix(const uchar *b, int n, const uchar *p, int m)
{
  int i;

  if(n < m)
    return 0;
  for(i = 0; i < m; i++)
    if(b[i] != p[i])
      return 0;
  return 1;
}

static int
looks_text(const uchar *b, int n)
{
  int i;
  int printable;

  if(n <= 0)
    return 0;

  printable = 0;
  for(i = 0; i < n; i++){
    uchar c = b[i];
    if(c == 0)
      return 0;
    if(c == '\n' || c == '\r' || c == '\t' || (c >= 32 && c <= 126))
      printable++;
  }

  return (printable * 100 / n) >= 85;
}

static const char*
detect_file_type(const uchar *b, int n)
{
  if(n == 0)
    return "empty";

  if(has_prefix(b, n, (const uchar*)"\x7f" "ELF", 4))
    return "ELF executable/object";
  if(has_prefix(b, n, (const uchar*)"#!", 2))
    return "script text executable";
  if(has_prefix(b, n, (const uchar*)"MZ", 2))
    return "DOS/PE executable";

  if(has_prefix(b, n, (const uchar*)"%PDF-", 5))
    return "PDF document";
  if(has_prefix(b, n, (const uchar*)"PK\x03\x04", 4) ||
     has_prefix(b, n, (const uchar*)"PK\x05\x06", 4) ||
     has_prefix(b, n, (const uchar*)"PK\x07\x08", 4))
    return "ZIP archive";
  if(has_prefix(b, n, (const uchar*)"\x1f\x8b", 2))
    return "gzip compressed data";
  if(has_prefix(b, n, (const uchar*)"BZh", 3))
    return "bzip2 compressed data";
  if(has_prefix(b, n, (const uchar*)"\xfd" "7zXZ\x00", 6))
    return "xz compressed data";
  if(has_prefix(b, n, (const uchar*)"7z\xbc\xaf\x27\x1c", 6))
    return "7-zip archive";

  if(has_prefix(b, n, (const uchar*)"\x89PNG\r\n\x1a\n", 8))
    return "PNG image";
  if(has_prefix(b, n, (const uchar*)"\xff\xd8\xff", 3))
    return "JPEG image";
  if(has_prefix(b, n, (const uchar*)"GIF87a", 6) || has_prefix(b, n, (const uchar*)"GIF89a", 6))
    return "GIF image";
  if(has_prefix(b, n, (const uchar*)"BM", 2))
    return "BMP image";
  if(n >= 12 && has_prefix(b, n, (const uchar*)"RIFF", 4) &&
     b[8] == 'W' && b[9] == 'E' && b[10] == 'B' && b[11] == 'P')
    return "WebP image";

  if(n >= 12 && has_prefix(b, n, (const uchar*)"RIFF", 4) &&
     b[8] == 'W' && b[9] == 'A' && b[10] == 'V' && b[11] == 'E')
    return "WAV audio";
  if(has_prefix(b, n, (const uchar*)"OggS", 4))
    return "Ogg media";
  if(has_prefix(b, n, (const uchar*)"fLaC", 4))
    return "FLAC audio";
  if(has_prefix(b, n, (const uchar*)"ID3", 3))
    return "MP3 audio";

  if(has_prefix(b, n, (const uchar*)"SQLite format 3\x00", 16))
    return "SQLite database";
  if(n > 262 && b[257] == 'u' && b[258] == 's' && b[259] == 't' && b[260] == 'a' && b[261] == 'r')
    return "tar archive";
  if(n > 1081 && b[1080] == 'M' && b[1081] == 'K')
    return "ProTracker module";

  if(n > 1082){
    ushort magic = (ushort)b[1080] | ((ushort)b[1081] << 8);
    if(magic == 0xef53)
      return "ext2/ext3/ext4 filesystem image";
  }

  if(looks_text(b, n))
    return "ASCII text";
  return "data";
}

static void
report_path(const char *path)
{
  struct stat st;
  int fd;
  int n;
  uchar buf[PROBE_SIZE];
  const char *kind;
  uchar iso[5];

  if(lstat(path, &st) < 0){
    dprintf(2, "file: cannot stat %s\n", path);
    return;
  }

  if(S_ISDIR(st.st_mode)){
    dprintf(1, "%s: directory\n", path);
    return;
  }
  if((S_ISCHR(st.st_mode) || S_ISBLK(st.st_mode))){
    dprintf(1, "%s: device\n", path);
    return;
  }
  if(S_ISLNK(st.st_mode)){
    dprintf(1, "%s: symbolic link\n", path);
    return;
  }

  fd = open(path, O_RDONLY);
  if(fd < 0){
    dprintf(2, "file: cannot open %s\n", path);
    return;
  }

  n = read(fd, buf, sizeof(buf));
  if(n < 0){
    close(fd);
    dprintf(2, "file: cannot read %s\n", path);
    return;
  }

  kind = detect_file_type(buf, n);
  if(strcmp(kind, "data") == 0){
    if(lseek(fd, 0x8001, SEEK_SET) >= 0 && read(fd, iso, sizeof(iso)) == (int)sizeof(iso)){
      if(iso[0] == 'C' && iso[1] == 'D' && iso[2] == '0' && iso[3] == '0' && iso[4] == '1')
        kind = "ISO-9660 image";
    }
  }
  close(fd);
  dprintf(1, "%s: %s\n", path, kind);
}

int
main(int argc, char *argv[])
{
  int i;

  if(argc < 2){
    dprintf(2, "usage: file path...\n");
    exit(0);
  }

  for(i = 1; i < argc; i++)
    report_path(argv[i]);

  exit(0);
}
