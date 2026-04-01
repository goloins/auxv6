#include "../include/types.h"
#include "../include/stat.h"
#include "../include/user.h"
#include "../include/fcntl.h"

void test_basic_symlink(void)
{
  int fd;
  struct stat st, st_link;

  printf(1, "=== Test 1: Basic symlink creation and following ===\n");

  // Create a test file
  unlink("/tmp/testfile");
  fd = open("/tmp/testfile", O_CREATE | O_WRONLY);
  if(fd < 0){
    printf(2, "Failed to create test file\n");
    return;
  }
  if(write(fd, "hello world\n", 12) != 12){
    printf(2, "Failed to write to test file\n");
    close(fd);
    return;
  }
  close(fd);

  // Create a symlink to the test file
  unlink("/tmp/testlink");
  if(symlink("/tmp/testfile", "/tmp/testlink") < 0){
    printf(2, "Failed to create symlink\n");
    return;
  }

  // Test stat (should follow symlink to target)
  if(stat("/tmp/testlink", &st) < 0){
    printf(2, "stat /tmp/testlink failed\n");
    return;
  }
  printf(1, "stat /tmp/testlink: type=%d size=%d\n", st.st_type, st.st_size);

  // Test lstat (should return link properties, not target)
  if(lstat("/tmp/testlink", &st_link) < 0){
    printf(2, "lstat /tmp/testlink failed\n");
    return;
  }
  printf(1, "lstat /tmp/testlink: type=%d size=%d\n", st_link.st_type, st_link.st_size);

  if(st_link.st_type == T_SYMLINK){
    printf(1, "✓ lstat correctly returned T_SYMLINK\n");
  } else {
    printf(2, "✗ lstat returned type %d instead of T_SYMLINK\n", st_link.st_type);
  }

  // Test reading through symlink
  printf(1, "Reading through symlink:\n");
  fd = open("/tmp/testlink", O_RDONLY);
  if(fd < 0){
    printf(2, "Failed to open symlink\n");
    return;
  }
  char buf[64];
  int nread = read(fd, buf, sizeof(buf) - 1);
  if(nread > 0){
    buf[nread] = 0;
    printf(1, "Read %d bytes: %s", nread, buf);
  } else {
    printf(2, "Failed to read through symlink\n");
  }
  close(fd);

  printf(1, "\n");
}

void test_symlink_chain(void)
{
  int fd;

  printf(1, "=== Test 2: Symlink chains ===\n");

  // Create chain: link1 -> link2 -> link3 -> file
  unlink("/tmp/chainfile");
  fd = open("/tmp/chainfile", O_CREATE | O_WRONLY);
  if(fd < 0){
    printf(2, "Failed to create chain file\n");
    return;
  }
  write(fd, "chain content\n", 14);
  close(fd);

  unlink("/tmp/link3");
  if(symlink("/tmp/chainfile", "/tmp/link3") < 0){
    printf(2, "Failed to create link3\n");
    return;
  }

  unlink("/tmp/link2");
  if(symlink("/tmp/link3", "/tmp/link2") < 0){
    printf(2, "Failed to create link2\n");
    return;
  }

  unlink("/tmp/link1");
  if(symlink("/tmp/link2", "/tmp/link1") < 0){
    printf(2, "Failed to create link1\n");
    return;
  }

  // Try to read through chain
  fd = open("/tmp/link1", O_RDONLY);
  if(fd < 0){
    printf(2, "Failed to open /tmp/link1\n");
    return;
  }
  char buf[64];
  int nread = read(fd, buf, sizeof(buf) - 1);
  if(nread > 0){
    buf[nread] = 0;
    printf(1, "Successfully read through chain: %s", buf);
  } else {
    printf(2, "Failed to read through symlink chain\n");
  }
  close(fd);

  printf(1, "\n");
}

void test_readlink(void)
{
  char buf[256];
  int len;

  printf(1, "=== Test 3: readlink syscall ===\n");

  // Create a symlink
  unlink("/tmp/readlinktest");
  if(symlink("/some/target/path", "/tmp/readlinktest") < 0){
    printf(2, "Failed to create symlink\n");
    return;
  }

  // Read the symlink target
  len = readlink("/tmp/readlinktest", buf, sizeof(buf) - 1);
  if(len < 0){
    printf(2, "readlink failed\n");
    return;
  }
  buf[len] = 0;
  printf(1, "readlink /tmp/readlinktest: %s\n", buf);

  if(strcmp(buf, "/some/target/path") == 0){
    printf(1, "✓ readlink returned correct target\n");
  } else {
    printf(2, "✗ readlink returned wrong target: %s\n", buf);
  }

  printf(1, "\n");
}

void test_intermediate_symlink(void)
{
  int fd;

  printf(1, "=== Test 4: Symlink in directory path ===\n");

  // Create /tmp/realdir/testfile
  mkdir("/tmp/realdir");
  fd = open("/tmp/realdir/testfile", O_CREATE | O_WRONLY);
  if(fd < 0){
    printf(2, "Failed to create /tmp/realdir/testfile\n");
    return;
  }
  write(fd, "real content\n", 13);
  close(fd);

  // Create /tmp/linkdir -> /tmp/realdir
  unlink("/tmp/linkdir");
  if(symlink("/tmp/realdir", "/tmp/linkdir") < 0){
    printf(2, "Failed to create /tmp/linkdir\n");
    return;
  }

  // Try to access through symlink
  fd = open("/tmp/linkdir/testfile", O_RDONLY);
  if(fd < 0){
    printf(2, "Failed to open /tmp/linkdir/testfile\n");
    return;
  }
  char buf[64];
  int nread = read(fd, buf, sizeof(buf) - 1);
  if(nread > 0){
    buf[nread] = 0;
    printf(1, "✓ Successfully accessed file through symlink dir: %s", buf);
  } else {
    printf(2, "✗ Failed to read through symlink dir\n");
  }
  close(fd);

  printf(1, "\n");
}

int
main(int argc, char *argv[])
{
  printf(1, "Symlink Testing Suite\n");
  printf(1, "======================\n\n");

  test_basic_symlink();
  test_symlink_chain();
  test_readlink();
  test_intermediate_symlink();

  printf(1, "=== All tests completed ===\n");
  exit();
}
