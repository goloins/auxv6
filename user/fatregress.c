#include "types.h"
#include "stat.h"
#include "user.h"
#include "fcntl.h"

#define FAT_MOUNT_PATH "/fat"
#define FAT_FSTYPE "msdosfs"
#define FAT_DEV_FLAGS MNT_MAKEDEV(HD_DISK_DEV(3))
#define FAT_GROW_SIZE 9216

static char writebuf[FAT_GROW_SIZE];
static char readbuf[FAT_GROW_SIZE];

static void
fail(char *msg, char *path)
{
  if(path)
    printf(1, "fatregress: FAIL %s %s\n", msg, path);
  else
    printf(1, "fatregress: FAIL %s\n", msg);
  exit();
}

static void
ensure_dir(char *path)
{
  int fd;

  if(mkdir(path) == 0)
    return;
  fd = open(path, O_RDONLY);
  if(fd < 0)
    fail("open dir", path);
  close(fd);
}

static void
read_expect(char *path, char *want)
{
  int fd;
  int want_len;
  int n;

  fd = open(path, O_RDONLY);
  if(fd < 0)
    fail("open", path);
  want_len = strlen(want);
  n = read(fd, readbuf, sizeof(readbuf));
  if(n != want_len){
    close(fd);
    fail("read length", path);
  }
  close(fd);
  if(strncmp(readbuf, want, want_len) != 0)
    fail("read contents", path);
}

static void
fill_pattern(char *buf, int len)
{
  int i;

  for(i = 0; i < len; i++)
    buf[i] = 'A' + (i % 23);
}

static void
expect_absent(char *path)
{
  struct stat st;

  if(stat(path, &st) >= 0)
    fail("expected absent", path);
}

static void
check_small_write(void)
{
  char *path;
  int fd;
  int n;

  path = "/fat/newfile.txt";
  unlink(path);

  fd = open(path, O_CREATE | O_RDWR | O_TRUNC);
  if(fd < 0)
    fail("create", path);
  n = write(fd, "abc123\n", 7);
  if(n != 7){
    close(fd);
    fail("write", path);
  }
  close(fd);

  read_expect(path, "abc123\n");

  if(unlink(path) < 0)
    fail("unlink", path);
  expect_absent(path);
  printf(1, "fatregress: ok small write\n");
}

static void
check_growth_and_truncate(void)
{
  char *path;
  int fd;
  int off;
  struct stat st;

  path = "/fat/grow.bin";
  unlink(path);
  fill_pattern(writebuf, sizeof(writebuf));

  fd = open(path, O_CREATE | O_WRONLY | O_TRUNC);
  if(fd < 0)
    fail("create", path);
  for(off = 0; off < sizeof(writebuf); off += 1024){
    int chunk;
    int n;

    chunk = sizeof(writebuf) - off;
    if(chunk > 1024)
      chunk = 1024;
    n = write(fd, writebuf + off, chunk);
    if(n != chunk){
      close(fd);
      fail("grow write", path);
    }
  }
  close(fd);

  if(stat(path, &st) < 0)
    fail("stat", path);
  if(st.size != sizeof(writebuf))
    fail("stat size", path);

  fd = open(path, O_RDONLY);
  if(fd < 0)
    fail("reopen", path);
  for(off = 0; off < sizeof(readbuf); off += 1024){
    int chunk;
    int n;

    chunk = sizeof(readbuf) - off;
    if(chunk > 1024)
      chunk = 1024;
    n = read(fd, readbuf + off, chunk);
    if(n != chunk){
      close(fd);
      fail("grow read", path);
    }
  }
  close(fd);

  for(off = 0; off < sizeof(writebuf); off++){
    if(readbuf[off] != writebuf[off])
      fail("grow verify", path);
  }

  fd = open(path, O_WRONLY | O_TRUNC);
  if(fd < 0)
    fail("truncate open", path);
  close(fd);

  if(stat(path, &st) < 0)
    fail("post-truncate stat", path);
  if(st.size != 0)
    fail("post-truncate size", path);

  if(unlink(path) < 0)
    fail("cleanup unlink", path);
  expect_absent(path);
  printf(1, "fatregress: ok growth/truncate\n");
}

int
main(void)
{
  if(umount(FAT_MOUNT_PATH) < 0){
  }

  ensure_dir(FAT_MOUNT_PATH);
  if(mount(FAT_MOUNT_PATH, FAT_FSTYPE, FAT_DEV_FLAGS) < 0)
    fail("mount", FAT_MOUNT_PATH);

  read_expect("/fat/hello.txt", "hello from auxv6 fat image\n");
  read_expect("/fat/subdir/note.txt", "subdirectory note from fat image\n");
  printf(1, "fatregress: ok seeded reads\n");

  check_small_write();
  check_growth_and_truncate();

  if(umount(FAT_MOUNT_PATH) < 0)
    fail("umount", FAT_MOUNT_PATH);

  printf(1, "fatregress: all checks passed\n");
  exit();
}
