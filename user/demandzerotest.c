#include "types.h"
#include "sys/stat.h"
#include "fcntl.h"
#include "auxv6/user.h"

#define DEMANDZEROTEST_PROFILE "2026-04-11-r1"
#define PAGE_BYTES 4096

struct fault_sample {
  int dispatches;
  int demand_zero;
  int sigsegv;
};

static int passed;
static int failed;

static int read_text(const char *path, char *buf, int max);

static int
ensure_procfs_ready(void)
{
  char probe[32];

  if(read_text("/proc/vmstat", probe, sizeof(probe)) >= 0)
    return 0;

  mkdir("/proc");
  if(mount("/proc", "procfs", 0, 0, 0) < 0){
    if(read_text("/proc/vmstat", probe, sizeof(probe)) >= 0)
      return 0;
    return -1;
  }

  if(read_text("/proc/vmstat", probe, sizeof(probe)) < 0)
    return -1;
  return 0;
}

static void
sample_init(struct fault_sample *s)
{
  memset(s, 0xff, sizeof(*s));
}

static int
read_text(const char *path, char *buf, int max)
{
  int fd;
  int n;
  int off;

  fd = open(path, O_RDONLY);
  if(fd < 0)
    return -1;
  off = 0;
  while(off < max - 1){
    n = read(fd, buf + off, max - 1 - off);
    if(n < 0){
      close(fd);
      return -1;
    }
    if(n == 0)
      break;
    off += n;
  }
  buf[off] = 0;
  close(fd);
  return off;
}

static int
find_kv_value(char *text, const char *key)
{
  int i;
  int j;

  for(i = 0; text[i]; i++){
    for(j = 0; key[j] && text[i + j] == key[j]; j++)
      ;
    if(key[j] != 0)
      continue;
    i += j;
    while(text[i] == ' ' || text[i] == '\t')
      i++;
    if(text[i] < '0' || text[i] > '9')
      continue;
    return atoi(&text[i]);
  }
  return -1;
}

static int
read_fault_sample(struct fault_sample *s)
{
  char buf[4096];

  sample_init(s);
  if(read_text("/proc/vmstat", buf, sizeof(buf)) < 0)
    return -1;
  s->dispatches = find_kv_value(buf, "vm_fault_dispatches");
  s->demand_zero = find_kv_value(buf, "vm_fault_demand_zero");
  s->sigsegv = find_kv_value(buf, "vm_fault_sigsegv");
  return 0;
}

static int
phase_user_touch(void)
{
  char *base;
  int i;

  base = sbrk(3 * PAGE_BYTES);
  if(base == (char*)-1)
    return -1;

  for(i = 0; i < 3; i++){
    if(base[i * PAGE_BYTES] != 0)
      return -1;
    base[i * PAGE_BYTES] = (char)(0x20 + i);
    if(base[i * PAGE_BYTES] != (char)(0x20 + i))
      return -1;
    if(base[i * PAGE_BYTES + 137] != 0)
      return -1;
  }
  return 0;
}

static int
phase_pipe_read_into_lazy(void)
{
  char *page;
  char want[32];
  int fds[2];
  int i;

  page = sbrk(PAGE_BYTES);
  if(page == (char*)-1)
    return -1;
  for(i = 0; i < (int)sizeof(want); i++)
    want[i] = (char)('a' + (i % 26));
  if(pipe(fds) < 0)
    return -1;
  if(write(fds[1], want, sizeof(want)) != sizeof(want)){
    close(fds[0]);
    close(fds[1]);
    return -1;
  }
  if(read(fds[0], page, sizeof(want)) != sizeof(want)){
    close(fds[0]);
    close(fds[1]);
    return -1;
  }
  for(i = 0; i < (int)sizeof(want); i++){
    if(page[i] != want[i]){
      close(fds[0]);
      close(fds[1]);
      return -1;
    }
  }
  close(fds[0]);
  close(fds[1]);
  return 0;
}

static int
phase_pipe_write_from_lazy(void)
{
  char *page;
  char got[32];
  int fds[2];
  int i;

  page = sbrk(PAGE_BYTES);
  if(page == (char*)-1)
    return -1;
  if(pipe(fds) < 0)
    return -1;
  if(write(fds[1], page, sizeof(got)) != sizeof(got)){
    close(fds[0]);
    close(fds[1]);
    return -1;
  }
  if(read(fds[0], got, sizeof(got)) != sizeof(got)){
    close(fds[0]);
    close(fds[1]);
    return -1;
  }
  for(i = 0; i < (int)sizeof(got); i++){
    if(got[i] != 0){
      close(fds[0]);
      close(fds[1]);
      return -1;
    }
  }
  close(fds[0]);
  close(fds[1]);
  return 0;
}

int
main(void)
{
  struct fault_sample before;
  struct fault_sample after;

  dprintf(1, "demandzerotest: profile=%s\n", DEMANDZEROTEST_PROFILE);
  if(ensure_procfs_ready() < 0){
    dprintf(2, "demandzerotest: /proc/vmstat unavailable even after procfs mount attempt\n");
    exit(1);
  }
  if(read_fault_sample(&before) < 0){
    dprintf(2, "demandzerotest: failed to read initial /proc/vmstat\n");
    exit(1);
  }

  if(phase_user_touch() == 0){
    dprintf(1, "[PASS] user touch phase\n");
    passed++;
  } else {
    dprintf(1, "[FAIL] user touch phase\n");
    failed++;
  }

  if(phase_pipe_read_into_lazy() == 0){
    dprintf(1, "[PASS] pipe read into lazy page\n");
    passed++;
  } else {
    dprintf(1, "[FAIL] pipe read into lazy page\n");
    failed++;
  }

  if(phase_pipe_write_from_lazy() == 0){
    dprintf(1, "[PASS] pipe write from lazy zero page\n");
    passed++;
  } else {
    dprintf(1, "[FAIL] pipe write from lazy zero page\n");
    failed++;
  }

  if(read_fault_sample(&after) < 0){
    dprintf(2, "demandzerotest: failed to read final /proc/vmstat\n");
    exit(1);
  }

  dprintf(1,
          "[DIAG] vm_fault_dispatches delta=%d vm_fault_demand_zero delta=%d vm_fault_sigsegv delta=%d\n",
          after.dispatches - before.dispatches,
          after.demand_zero - before.demand_zero,
          after.sigsegv - before.sigsegv);
  if(after.demand_zero <= before.demand_zero){
    dprintf(1, "[FAIL] demand-zero counter did not move\n");
    failed++;
  } else {
    dprintf(1, "[PASS] demand-zero counter moved\n");
    passed++;
  }
  if(after.sigsegv != before.sigsegv){
    dprintf(1, "[FAIL] sigsegv counter changed unexpectedly\n");
    failed++;
  } else {
    dprintf(1, "[PASS] sigsegv counter stable\n");
    passed++;
  }

  dprintf(1, "demandzerotest: passed=%d failed=%d\n", passed, failed);
  exit(failed ? 1 : 0);
}