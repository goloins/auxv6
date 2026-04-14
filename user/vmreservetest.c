#include "types.h"
#include "sys/stat.h"
#include "auxv6/user.h"

#define VMRESERVETEST_PROFILE "2026-04-11-r1"

#define RESERVE_PAGES 4
#define RESERVE_BYTES (RESERVE_PAGES * 4096)

static int passed;
static int failed;

#define PASS(name) do { dprintf(1, "[PASS] %s\n", name); passed++; } while(0)
#define FAIL(name, why) do { dprintf(1, "[FAIL] %s: %s\n", name, why); failed++; } while(0)

static void
usage(void)
{
  dprintf(2, "usage: vmreservetest\n");
}

static int
check_zero_page(char *page, int page_index)
{
  int i;

  for(i = 0; i < 4096; i++){
    if(page[i] != 0){
      dprintf(1,
              "[DIAG] page %d not zero at byte %d: got=0x%x\n",
              page_index, i, (uchar)page[i]);
      return -1;
    }
  }
  return 0;
}

static int
check_fill_page(char *page, int len, char value, const char *tag)
{
  int i;

  for(i = 0; i < len; i++){
    if(page[i] != value){
      dprintf(1,
              "[DIAG] %s mismatch at byte %d: got=0x%x want=0x%x\n",
              tag, i, (uchar)page[i], (uchar)value);
      return -1;
    }
  }
  return 0;
}

static void
phase_first_touch_zero(char *region)
{
  if(check_zero_page(region + 2 * 4096, 2) < 0){
    FAIL("first touch sparse zero", "third page did not fault in as zero");
    return;
  }
  if(check_zero_page(region, 0) < 0){
    FAIL("first touch leading zero", "first page did not fault in as zero");
    return;
  }
  PASS("first touch zero fill");
}

static void
phase_write_persist(char *region)
{
  memset(region, 'A', 4096);
  memset(region + 2 * 4096, 'C', 4096);

  if(check_fill_page(region, 4096, 'A', "page0") < 0){
    FAIL("write persistence page0", "pattern did not stick");
    return;
  }
  if(check_fill_page(region + 2 * 4096, 4096, 'C', "page2") < 0){
    FAIL("write persistence page2", "pattern did not stick");
    return;
  }
  PASS("write persistence");
}

static void
phase_syscall_copyout(char *region)
{
  int fds[2];
  char payload[64];
  int i;

  for(i = 0; i < (int)sizeof(payload) - 1; i++)
    payload[i] = (char)('a' + (i % 26));
  payload[sizeof(payload) - 1] = 0;

  if(pipe(fds) < 0){
    FAIL("syscall copyout zerofill", "pipe failed");
    return;
  }
  if(write(fds[1], payload, sizeof(payload)) != sizeof(payload)){
    close(fds[0]);
    close(fds[1]);
    FAIL("syscall copyout zerofill", "pipe write failed");
    return;
  }
  if(read(fds[0], region + 3 * 4096, sizeof(payload)) != sizeof(payload)){
    close(fds[0]);
    close(fds[1]);
    FAIL("syscall copyout zerofill", "pipe read into reserved page failed");
    return;
  }
  close(fds[0]);
  close(fds[1]);

  for(i = 0; i < (int)sizeof(payload); i++){
    if(region[3 * 4096 + i] != payload[i]){
      dprintf(1,
              "[DIAG] syscall copyout mismatch at byte %d: got=0x%x want=0x%x\n",
              i, (uchar)region[3 * 4096 + i], (uchar)payload[i]);
      FAIL("syscall copyout zerofill", "copied bytes mismatch");
      return;
    }
  }

  PASS("syscall copyout zerofill");
}

static void
phase_fork_isolation(char *region)
{
  int pid;
  int st;

  pid = fork();
  if(pid < 0){
    FAIL("fork isolation", "fork failed");
    return;
  }

  if(pid == 0){
    if(check_fill_page(region, 4096, 'A', "child inherited page0") < 0)
      exit(10);
    if(check_fill_page(region + 2 * 4096, 4096, 'C', "child inherited page2") < 0)
      exit(11);
    if(check_zero_page(region + 4096, 1) < 0)
      exit(12);

    memset(region, 'B', 4096);
    memset(region + 4096, 'D', 4096);

    if(check_fill_page(region, 4096, 'B', "child private page0") < 0)
      exit(13);
    if(check_fill_page(region + 4096, 4096, 'D', "child private page1") < 0)
      exit(14);
    if(check_fill_page(region + 2 * 4096, 4096, 'C', "child preserved page2") < 0)
      exit(15);
    exit(0);
  }

  if(waitpid(pid, &st, 0) != pid){
    FAIL("fork isolation", "waitpid failed");
    return;
  }
  if(!WIFEXITED(st) || WEXITSTATUS(st) != 0){
    dprintf(1, "[DIAG] child status=0x%x\n", st);
    FAIL("fork isolation", "child observed wrong zerofill or COW behavior");
    return;
  }

  if(check_fill_page(region, 4096, 'A', "parent preserved page0") < 0){
    FAIL("fork isolation parent page0", "parent page0 changed after child write");
    return;
  }
  if(check_zero_page(region + 4096, 1) < 0){
    FAIL("fork isolation parent page1", "parent untouched reserved page was modified");
    return;
  }
  if(check_fill_page(region + 2 * 4096, 4096, 'C', "parent preserved page2") < 0){
    FAIL("fork isolation parent page2", "parent page2 changed after child work");
    return;
  }

  PASS("fork isolation");
}

int
main(int argc, char *argv[])
{
  char *region;

  if(argc != 1){
    usage();
    exit(1);
  }

  region = (char*)vmreserve(RESERVE_BYTES);
  if(region == (char*)-1){
    dprintf(2, "vmreservetest: vmreserve failed\n");
    exit(1);
  }
  if(((uint)region % 4096) != 0){
    dprintf(2, "vmreservetest: unaligned reservation %p\n", region);
    exit(1);
  }

  dprintf(1,
          "vmreservetest: profile=%s reserve=%d base=%p\n",
          VMRESERVETEST_PROFILE, RESERVE_BYTES, region);

  phase_first_touch_zero(region);
  phase_write_persist(region);
  phase_syscall_copyout(region);
  phase_fork_isolation(region);

  dprintf(1, "vmreservetest: passed=%d failed=%d\n", passed, failed);
  exit(failed == 0 ? 0 : 1);
}