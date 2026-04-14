#include "types.h"
#include "sys/stat.h"
#include "fcntl.h"
#include "auxv6/user.h"

#define VMGUARDTEST_PROFILE "2026-04-11-r1"

#define DEFAULT_PROCFS_ITERS 64
#define DEFAULT_PIPE_ITERS 64
#define DEFAULT_FORK_ITERS 32
#define PROC_BUF_SZ 1024
#define VMSTAT_BUF_SZ 4096
#define PIPE_CHUNK 256

#define PASS(name) do { dprintf(1, "[PASS] %s\n", name); passed++; } while(0)
#define FAIL(name, why) do { dprintf(1, "[FAIL] %s: %s\n", name, why); failed++; } while(0)

struct guard_sample {
  int checks;
  int allows;
  int denies;
  int bypass_no_as;
  int bypass_vm_size;
};

static int passed;
static int failed;

static void
sample_init(struct guard_sample *s)
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
read_guard_sample(struct guard_sample *s)
{
  char buf[VMSTAT_BUF_SZ];

  sample_init(s);
  if(read_text("/proc/vmstat", buf, sizeof(buf)) < 0)
    return -1;

  s->checks = find_kv_value(buf, "vm_as_guard_checks");
  s->allows = find_kv_value(buf, "vm_as_guard_allows");
  s->denies = find_kv_value(buf, "vm_as_guard_denies");
  s->bypass_no_as = find_kv_value(buf, "vm_as_guard_bypass_no_as");
  s->bypass_vm_size = find_kv_value(buf, "vm_as_guard_bypass_vm_size");
  return 0;
}

static int
delta_or_fail(int after, int before)
{
  if(after < 0 || before < 0)
    return -1;
  return after - before;
}

static void
print_delta(const char *tag, struct guard_sample *before, struct guard_sample *after)
{
  dprintf(1,
          "[DIAG] %s: d_checks=%d d_allows=%d d_denies=%d d_bypass_no_as=%d d_bypass_vm_size=%d\n",
          tag,
          delta_or_fail(after->checks, before->checks),
          delta_or_fail(after->allows, before->allows),
          delta_or_fail(after->denies, before->denies),
          delta_or_fail(after->bypass_no_as, before->bypass_no_as),
          delta_or_fail(after->bypass_vm_size, before->bypass_vm_size));
}

static int
phase_procfs_reads(int iters)
{
  char buf[PROC_BUF_SZ];
  int i;

  for(i = 0; i < iters; i++){
    if(read_text("/proc/meminfo", buf, sizeof(buf)) <= 0)
      return -1;
  }
  return 0;
}

static int
phase_pipe_roundtrip(int iters)
{
  char wbuf[PIPE_CHUNK];
  char rbuf[PIPE_CHUNK];
  int fds[2];
  int i;
  int j;

  for(i = 0; i < PIPE_CHUNK; i++)
    wbuf[i] = (char)('a' + (i % 26));

  for(i = 0; i < iters; i++){
    if(pipe(fds) < 0)
      return -1;
    if(write(fds[1], wbuf, sizeof(wbuf)) != sizeof(wbuf)){
      close(fds[0]);
      close(fds[1]);
      return -1;
    }
    if(read(fds[0], rbuf, sizeof(rbuf)) != sizeof(rbuf)){
      close(fds[0]);
      close(fds[1]);
      return -1;
    }
    for(j = 0; j < PIPE_CHUNK; j++){
      if(rbuf[j] != wbuf[j]){
        close(fds[0]);
        close(fds[1]);
        return -1;
      }
    }
    close(fds[0]);
    close(fds[1]);
  }
  return 0;
}

static int
phase_forked_procfs_reads(int iters)
{
  int i;
  int pid;
  int st;
  char buf[PROC_BUF_SZ];

  for(i = 0; i < iters; i++){
    pid = fork();
    if(pid < 0)
      return -1;
    if(pid == 0){
      if(read_text("/proc/meminfo", buf, sizeof(buf)) <= 0)
        exit(2);
      exit(0);
    }
    if(waitpid(pid, &st, 0) != pid)
      return -1;
    if(!WIFEXITED(st) || WEXITSTATUS(st) != 0)
      return -1;
  }
  return 0;
}

static void
check_guard_delta(const char *name, struct guard_sample *before,
                  struct guard_sample *after, int require_checks)
{
  int d_checks;
  int d_denies;
  int d_bypass_no_as;
  int d_bypass_vm_size;

  d_checks = delta_or_fail(after->checks, before->checks);
  d_denies = delta_or_fail(after->denies, before->denies);
  d_bypass_no_as = delta_or_fail(after->bypass_no_as, before->bypass_no_as);
  d_bypass_vm_size = delta_or_fail(after->bypass_vm_size, before->bypass_vm_size);

  print_delta(name, before, after);
  if(d_checks < 0 || d_denies < 0 || d_bypass_no_as < 0 || d_bypass_vm_size < 0){
    FAIL(name, "missing vmstat keys");
    return;
  }
  if(require_checks && d_checks <= 0){
    FAIL(name, "guard checks did not move");
    return;
  }
  if(d_denies != 0){
    FAIL(name, "vm_as_guard_denies increased");
    return;
  }
  if(d_bypass_vm_size != 0){
    FAIL(name, "vm_as_guard_bypass_vm_size increased");
    return;
  }
  if(d_bypass_no_as != 0){
    FAIL(name, "vm_as_guard_bypass_no_as increased");
    return;
  }
  PASS(name);
}

static void
usage(void)
{
  dprintf(2,
          "usage: vmguardtest [-p procfs_iters] [-P pipe_iters] [-f fork_iters]\n");
}

int
main(int argc, char *argv[])
{
  int i;
  int procfs_iters;
  int pipe_iters;
  int fork_iters;
  struct guard_sample before;
  struct guard_sample after;

  procfs_iters = DEFAULT_PROCFS_ITERS;
  pipe_iters = DEFAULT_PIPE_ITERS;
  fork_iters = DEFAULT_FORK_ITERS;

  for(i = 1; i < argc; i++){
    if(strcmp(argv[i], "-h") == 0 || strcmp(argv[i], "--help") == 0){
      usage();
      exit(0);
    }
    if(strcmp(argv[i], "-p") == 0 && i + 1 < argc){
      procfs_iters = atoi(argv[++i]);
      continue;
    }
    if(strcmp(argv[i], "-P") == 0 && i + 1 < argc){
      pipe_iters = atoi(argv[++i]);
      continue;
    }
    if(strcmp(argv[i], "-f") == 0 && i + 1 < argc){
      fork_iters = atoi(argv[++i]);
      continue;
    }
    usage();
    exit(1);
  }

  if(procfs_iters < 1 || pipe_iters < 1 || fork_iters < 1){
    dprintf(2, "vmguardtest: invalid arguments\n");
    exit(1);
  }

  dprintf(1,
          "vmguardtest: profile=%s procfs_iters=%d pipe_iters=%d fork_iters=%d\n",
          VMGUARDTEST_PROFILE, procfs_iters, pipe_iters, fork_iters);

  if(read_guard_sample(&before) < 0){
    dprintf(2, "vmguardtest: failed to read initial /proc/vmstat\n");
    exit(1);
  }
  after = before;

  if(phase_procfs_reads(procfs_iters) < 0){
    FAIL("procfs current-proc phase", "procfs read failure");
  } else if(read_guard_sample(&after) < 0){
    FAIL("procfs current-proc phase", "post-phase vmstat read failure");
  } else {
    check_guard_delta("procfs current-proc phase", &before, &after, 1);
  }

  before = after;
  if(phase_pipe_roundtrip(pipe_iters) < 0){
    FAIL("pipe current-proc phase", "pipe roundtrip failure");
  } else if(read_guard_sample(&after) < 0){
    FAIL("pipe current-proc phase", "post-phase vmstat read failure");
  } else {
    check_guard_delta("pipe current-proc phase", &before, &after, 1);
  }

  before = after;
  if(phase_forked_procfs_reads(fork_iters) < 0){
    FAIL("forked child procfs phase", "child procfs read failure");
  } else if(read_guard_sample(&after) < 0){
    FAIL("forked child procfs phase", "post-phase vmstat read failure");
  } else {
    check_guard_delta("forked child procfs phase", &before, &after, 1);
  }

  dprintf(1, "vmguardtest: passed=%d failed=%d\n", passed, failed);
  exit(failed ? 1 : 0);
}