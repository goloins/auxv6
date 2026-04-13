#include "types.h"
#include "stat.h"
#include "auxv6/user.h"
#include "fcntl.h"

void test_basic_symlink(void)
{
  int fd;
  struct stat st, st_link;

  dprintf(1, "=== Test 1: Basic symlink creation and following ===\n");

  // Create a test file
  unlink("/tmp/testfile");
  fd = open("/tmp/testfile", O_CREATE | O_WRONLY);
  if(fd < 0){
    dprintf(2, "Failed to create test file\n");
    return;
  }
  if(write(fd, "hello world\n", 12) != 12){
    dprintf(2, "Failed to write to test file\n");
    close(fd);
    return;
  }
  close(fd);

  // Create a symlink to the test file
  unlink("/tmp/testlink");
  if(symlink("/tmp/testfile", "/tmp/testlink") < 0){
    dprintf(2, "Failed to create symlink\n");
    return;
  }

  // Test stat (should follow symlink to target)
  if(stat("/tmp/testlink", &st) < 0){
    dprintf(2, "stat /tmp/testlink failed\n");
    return;
  }
  dprintf(1, "stat /tmp/testlink: type=%d size=%d\n", st.st_type, st.st_size);

  // Test lstat (should return link properties, not target)
  if(lstat("/tmp/testlink", &st_link) < 0){
    dprintf(2, "lstat /tmp/testlink failed\n");
    return;
  }
  dprintf(1, "lstat /tmp/testlink: type=%d size=%d\n", st_link.st_type, st_link.st_size);

  if(st_link.st_type == T_SYMLINK){
    dprintf(1, "✓ lstat correctly returned T_SYMLINK\n");
  } else {
    dprintf(2, "✗ lstat returned type %d instead of T_SYMLINK\n", st_link.st_type);
  }

  // Test reading through symlink
  dprintf(1, "Reading through symlink:\n");
  fd = open("/tmp/testlink", O_RDONLY);
  if(fd < 0){
    dprintf(2, "Failed to open symlink\n");
    return;
  }
  char buf[64];
  int nread = read(fd, buf, sizeof(buf) - 1);
  if(nread > 0){
    buf[nread] = 0;
    dprintf(1, "Read %d bytes: %s", nread, buf);
  } else {
    dprintf(2, "Failed to read through symlink\n");
  }
  close(fd);

  dprintf(1, "\n");
}

void test_symlink_chain(void)
{
  int fd;

  dprintf(1, "=== Test 2: Symlink chains ===\n");

  // Create chain: link1 -> link2 -> link3 -> file
  unlink("/tmp/chainfile");
  fd = open("/tmp/chainfile", O_CREATE | O_WRONLY);
  if(fd < 0){
    dprintf(2, "Failed to create chain file\n");
    return;
  }
  write(fd, "chain content\n", 14);
  close(fd);

  unlink("/tmp/link3");
  if(symlink("/tmp/chainfile", "/tmp/link3") < 0){
    dprintf(2, "Failed to create link3\n");
    return;
  }

  unlink("/tmp/link2");
  if(symlink("/tmp/link3", "/tmp/link2") < 0){
    dprintf(2, "Failed to create link2\n");
    return;
  }

  unlink("/tmp/link1");
  if(symlink("/tmp/link2", "/tmp/link1") < 0){
    dprintf(2, "Failed to create link1\n");
    return;
  }

  // Try to read through chain
  fd = open("/tmp/link1", O_RDONLY);
  if(fd < 0){
    dprintf(2, "Failed to open /tmp/link1\n");
    return;
  }
  char buf[64];
  int nread = read(fd, buf, sizeof(buf) - 1);
  if(nread > 0){
    buf[nread] = 0;
    dprintf(1, "Successfully read through chain: %s", buf);
  } else {
    dprintf(2, "Failed to read through symlink chain\n");
  }
  close(fd);

  dprintf(1, "\n");
}

void test_readlink(void)
{
  char buf[256];
  int len;

  dprintf(1, "=== Test 3: readlink syscall ===\n");

  // Create a symlink
  unlink("/tmp/readlinktest");
  if(symlink("/some/target/path", "/tmp/readlinktest") < 0){
    dprintf(2, "Failed to create symlink\n");
    return;
  }

  // Read the symlink target
  len = readlink("/tmp/readlinktest", buf, sizeof(buf) - 1);
  if(len < 0){
    dprintf(2, "readlink failed\n");
    return;
  }
  buf[len] = 0;
  dprintf(1, "readlink /tmp/readlinktest: %s\n", buf);

  if(strcmp(buf, "/some/target/path") == 0){
    dprintf(1, "✓ readlink returned correct target\n");
  } else {
    dprintf(2, "✗ readlink returned wrong target: %s\n", buf);
  }

  dprintf(1, "\n");
}

void test_intermediate_symlink(void)
{
  int fd;

  dprintf(1, "=== Test 4: Symlink in directory path ===\n");

  // Create /tmp/realdir/testfile
  mkdir("/tmp/realdir");
  fd = open("/tmp/realdir/testfile", O_CREATE | O_WRONLY);
  if(fd < 0){
    dprintf(2, "Failed to create /tmp/realdir/testfile\n");
    return;
  }
  write(fd, "real content\n", 13);
  close(fd);

  // Create /tmp/linkdir -> /tmp/realdir
  unlink("/tmp/linkdir");
  if(symlink("/tmp/realdir", "/tmp/linkdir") < 0){
    dprintf(2, "Failed to create /tmp/linkdir\n");
    return;
  }

  // Try to access through symlink
  fd = open("/tmp/linkdir/testfile", O_RDONLY);
  if(fd < 0){
    dprintf(2, "Failed to open /tmp/linkdir/testfile\n");
    return;
  }
  char buf[64];
  int nread = read(fd, buf, sizeof(buf) - 1);
  if(nread > 0){
    buf[nread] = 0;
    dprintf(1, "✓ Successfully accessed file through symlink dir: %s", buf);
  } else {
    dprintf(2, "✗ Failed to read through symlink dir\n");
  }
  close(fd);

  dprintf(1, "\n");
}

void test_symlink_loop_detection(void)
{
  int fd;
  struct stat st;

  dprintf(1, "=== Test 5: Symlink loop detection ===\n");

  unlink("/tmp/loop_a");
  unlink("/tmp/loop_b");

  if(symlink("/tmp/loop_b", "/tmp/loop_a") < 0){
    dprintf(2, "Failed to create /tmp/loop_a\n");
    return;
  }
  if(symlink("/tmp/loop_a", "/tmp/loop_b") < 0){
    dprintf(2, "Failed to create /tmp/loop_b\n");
    return;
  }

  // lstat should still succeed on the link inode itself.
  if(lstat("/tmp/loop_a", &st) < 0){
    dprintf(2, "lstat /tmp/loop_a failed\n");
    return;
  }
  if(st.st_type == T_SYMLINK)
    dprintf(1, "✓ lstat works on loop member\n");
  else
    dprintf(2, "✗ lstat type mismatch on loop member: %d\n", st.st_type);

  // Following should fail because this is an infinite loop.
  if(stat("/tmp/loop_a", &st) < 0){
    dprintf(1, "✓ stat failed on symlink loop as expected\n");
  } else {
    dprintf(2, "✗ stat unexpectedly succeeded on symlink loop\n");
  }

  fd = open("/tmp/loop_a", O_RDONLY);
  if(fd < 0){
    dprintf(1, "✓ open failed on symlink loop as expected\n");
  } else {
    dprintf(2, "✗ open unexpectedly succeeded on symlink loop\n");
    close(fd);
  }

  dprintf(1, "\n");
}

void test_relative_symlink_edges(void)
{
  int fd;
  char buf[64];
  int nread;

  dprintf(1, "=== Test 6: Relative symlink edge cases ===\n");

  mkdir("/tmp/relsym");
  mkdir("/tmp/relsym/base");
  mkdir("/tmp/relsym/one");
  mkdir("/tmp/relsym/one/two");

  unlink("/tmp/relsym/base/file");
  fd = open("/tmp/relsym/base/file", O_CREATE | O_WRONLY);
  if(fd < 0){
    dprintf(2, "Failed to create /tmp/relsym/base/file\n");
    return;
  }
  write(fd, "rel content\n", 12);
  close(fd);

  // ./ target relative to link directory.
  unlink("/tmp/relsym/dot_link");
  if(symlink("./base", "/tmp/relsym/dot_link") < 0){
    dprintf(2, "Failed to create dot_link\n");
    return;
  }

  fd = open("/tmp/relsym/dot_link/file", O_RDONLY);
  if(fd < 0){
    dprintf(2, "✗ Failed to open through ./ relative symlink\n");
    return;
  }
  nread = read(fd, buf, sizeof(buf) - 1);
  close(fd);
  if(nread > 0){
    buf[nread] = 0;
    dprintf(1, "✓ ./ relative symlink resolved: %s", buf);
  } else {
    dprintf(2, "✗ Read failed through ./ relative symlink\n");
    return;
  }

  // ../ target from a nested directory.
  unlink("/tmp/relsym/one/two/up_link");
  if(symlink("../../base", "/tmp/relsym/one/two/up_link") < 0){
    dprintf(2, "Failed to create up_link\n");
    return;
  }

  fd = open("/tmp/relsym/one/two/up_link/file", O_RDONLY);
  if(fd < 0){
    dprintf(2, "✗ Failed to open through ../ relative symlink\n");
    return;
  }
  nread = read(fd, buf, sizeof(buf) - 1);
  close(fd);
  if(nread > 0){
    buf[nread] = 0;
    dprintf(1, "✓ ../ relative symlink resolved: %s", buf);
  } else {
    dprintf(2, "✗ Read failed through ../ relative symlink\n");
    return;
  }

  // Multiple slashes should normalize during resolution.
  fd = open("/tmp//relsym///dot_link//file", O_RDONLY);
  if(fd < 0){
    dprintf(2, "✗ Failed to open path with repeated slashes\n");
    return;
  }
  nread = read(fd, buf, sizeof(buf) - 1);
  close(fd);
  if(nread > 0)
    dprintf(1, "✓ Repeated-slash path resolved through symlink\n");
  else
    dprintf(2, "✗ Read failed for repeated-slash path\n");

  dprintf(1, "\n");
}

void test_symlink_depth_boundary(void)
{
  int i;
  int fd;
  struct stat st;
  char name[64];
  char target[64];

  dprintf(1, "=== Test 7: Symlink depth boundary ===\n");

  unlink("/tmp/depth_file");
  fd = open("/tmp/depth_file", O_CREATE | O_WRONLY);
  if(fd < 0){
    dprintf(2, "Failed to create /tmp/depth_file\n");
    return;
  }
  write(fd, "depth content\n", 14);
  close(fd);

  // Build link0..link8 where link8 -> depth_file (9 links total from link0).
  for(i = 0; i <= 8; i++){
    name[0] = 0;
    target[0] = 0;
    strcpy(name, "/tmp/depth_link");
    name[15] = '0' + i;
    name[16] = 0;
    unlink(name);

    if(i == 8){
      strcpy(target, "/tmp/depth_file");
    } else {
      strcpy(target, "/tmp/depth_link");
      target[15] = '0' + (i + 1);
      target[16] = 0;
    }

    if(symlink(target, name) < 0){
      dprintf(2, "Failed to create %s -> %s\n", name, target);
      return;
    }
  }

  // 9-link chain should fail when SYMLOOP_MAX is 8.
  if(stat("/tmp/depth_link0", &st) < 0){
    dprintf(1, "✓ 9-link chain failed as expected\n");
  } else {
    dprintf(2, "✗ 9-link chain unexpectedly resolved\n");
  }

  // 8-link chain (start at link1) should still succeed.
  if(stat("/tmp/depth_link1", &st) < 0){
    dprintf(2, "✗ 8-link chain unexpectedly failed\n");
  } else {
    dprintf(1, "✓ 8-link chain resolved successfully\n");
  }

  fd = open("/tmp/depth_link1", O_RDONLY);
  if(fd < 0){
    dprintf(2, "✗ open failed on 8-link chain\n");
  } else {
    char buf[64];
    int nread = read(fd, buf, sizeof(buf) - 1);
    close(fd);
    if(nread > 0)
      dprintf(1, "✓ open/read succeeded on 8-link chain\n");
    else
      dprintf(2, "✗ read failed on 8-link chain\n");
  }

  dprintf(1, "\n");
}

int
main(int argc, char *argv[])
{
  dprintf(1, "Symlink Testing Suite\n");
  dprintf(1, "======================\n\n");

  // Ensure /tmp exists
  mkdir("/tmp");

  test_basic_symlink();
  test_symlink_chain();
  test_readlink();
  test_intermediate_symlink();
  test_symlink_loop_detection();
  test_relative_symlink_edges();
  test_symlink_depth_boundary();

  dprintf(1, "=== All tests completed ===\n");
  exit(0);
}
