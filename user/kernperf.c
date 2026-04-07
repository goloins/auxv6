// kernperf.c - general kernel performance ruler
//
// Purpose:
// - Provide a system-agnostic, general before/after kernel perf utility.
// - Measure common kernel paths without relying on subsystem-specific procfs
//   counters: syscall overhead, process churn, IPC wakeups, VM page touch,
//   and filesystem I/O throughput.
//
// Usage:
//   kernperf [-n runs]
//
// Output:
// - [PERF] lines per run and test
// - Final averaged score (0..100)
// - Non-zero exit only on functional test failures

#include "types.h"
#include "stat.h"
#include "fcntl.h"
#include "auxv6/user.h"

#define KERNPERF_PROFILE "2026-04-06-r1"

#define MAX_RUNS 20
#define DEFAULT_RUNS 3

#define KB 1024
#define MB (1024 * 1024)

struct bench_case {
  const char *name;
  const char *unit;
  int target;
  int weight;
  int (*run)(int *value);
};

static int
ops_per_sec(int ops, uint start_ticks, uint end_ticks)
{
  uint dt;

  dt = (end_ticks > start_ticks) ? (end_ticks - start_ticks) : 1;
  return (int)((ops * 100U) / dt);
}

static int
kb_per_sec(int bytes, uint start_ticks, uint end_ticks)
{
  uint dt;
  uint kb;

  dt = (end_ticks > start_ticks) ? (end_ticks - start_ticks) : 1;
  kb = (uint)((bytes + (KB - 1)) / KB);
  return (int)((kb * 100U) / dt);
}

static int
score_points(int value, int target, int weight)
{
  long long scaled;

  if(target <= 0)
    return 0;
  if(value >= target)
    return weight;

  scaled = (long long)value * (long long)weight;
  return (int)((scaled + (target / 2)) / target);
}

static int
bench_syscall_getpid(int *value)
{
  int i;
  int n;
  uint t0;
  uint t1;
  volatile int sink;

  n = 25000;
  sink = 0;

  t0 = uptime();
  for(i = 0; i < n; i++)
    sink ^= getpid();
  t1 = uptime();

  if(sink == -1)
    return -1;

  *value = ops_per_sec(n, t0, t1);
  return 0;
}

static int
bench_fork_wait(int *value)
{
  int i;
  int n;
  int pid;
  uint t0;
  uint t1;

  n = 64;

  t0 = uptime();
  for(i = 0; i < n; i++){
    pid = fork();
    if(pid < 0)
      return -1;
    if(pid == 0)
      exit(0);
    if(wait() < 0)
      return -1;
  }
  t1 = uptime();

  *value = ops_per_sec(n, t0, t1);
  return 0;
}

static int
bench_pipe_pingpong(int *value)
{
  int p2c[2];
  int c2p[2];
  int i;
  int rounds;
  int pid;
  char b;
  uint t0;
  uint t1;

  rounds = 1500;

  if(pipe(p2c) < 0)
    return -1;
  if(pipe(c2p) < 0){
    close(p2c[0]);
    close(p2c[1]);
    return -1;
  }

  pid = fork();
  if(pid < 0)
    return -1;

  if(pid == 0){
    close(p2c[1]);
    close(c2p[0]);
    for(i = 0; i < rounds; i++){
      if(read(p2c[0], &b, 1) != 1)
        exit(1);
      if(write(c2p[1], &b, 1) != 1)
        exit(1);
    }
    close(p2c[0]);
    close(c2p[1]);
    exit(0);
  }

  close(p2c[0]);
  close(c2p[1]);

  b = 0x5a;
  t0 = uptime();
  for(i = 0; i < rounds; i++){
    if(write(p2c[1], &b, 1) != 1)
      return -1;
    if(read(c2p[0], &b, 1) != 1)
      return -1;
  }
  t1 = uptime();

  close(p2c[1]);
  close(c2p[0]);
  if(wait() < 0)
    return -1;

  *value = ops_per_sec(rounds, t0, t1);
  return 0;
}

static int
bench_vm_page_touch(int *value)
{
  int i;
  int pages;
  int bytes;
  char *base;
  char *p;
  volatile uint sum;
  uint t0;
  uint t1;

  pages = 96;
  bytes = pages * 4096;
  sum = 0;

  base = sbrk(bytes);
  if((int)base == -1)
    return -1;

  t0 = uptime();
  for(i = 0; i < pages; i++){
    p = base + i * 4096;
    p[0] = (char)i;
    sum += (uchar)p[0];
  }
  for(i = pages - 1; i >= 0; i--){
    p = base + i * 4096;
    p[0] ^= 0x3;
    sum += (uchar)p[0];
  }
  t1 = uptime();

  if(sum == 0xffffffffU)
    return -1;

  *value = kb_per_sec(bytes * 2, t0, t1);
  return 0;
}

static int
bench_file_io(int *value)
{
  char buf[512];
  const char *path;
  int fd;
  int i;
  int blocks;
  int total;
  uint t0;
  uint t1;

  blocks = 256;
  total = blocks * sizeof(buf);
  path = "/tmp/kernperf_io.tmp";

  for(i = 0; i < (int)sizeof(buf); i++)
    buf[i] = (char)(i & 0x7f);

  fd = open(path, O_CREATE | O_WRONLY);
  if(fd < 0)
    return -1;

  t0 = uptime();
  for(i = 0; i < blocks; i++){
    if(write(fd, buf, sizeof(buf)) != (int)sizeof(buf)){
      close(fd);
      unlink(path);
      return -1;
    }
  }
  close(fd);

  fd = open(path, O_RDONLY);
  if(fd < 0){
    unlink(path);
    return -1;
  }
  for(i = 0; i < blocks; i++){
    if(read(fd, buf, sizeof(buf)) != (int)sizeof(buf)){
      close(fd);
      unlink(path);
      return -1;
    }
  }
  t1 = uptime();

  close(fd);
  unlink(path);

  *value = kb_per_sec(total * 2, t0, t1);
  return 0;
}

static struct bench_case benches[] = {
  { "syscall-getpid", "ops/s", 180000, 18, bench_syscall_getpid },
  { "fork-wait",      "ops/s",    220, 20, bench_fork_wait },
  { "pipe-pingpong",  "ops/s",   2200, 22, bench_pipe_pingpong },
  { "vm-page-touch",  "KB/s",   38000, 20, bench_vm_page_touch },
  { "file-io",        "KB/s",   18000, 20, bench_file_io },
};

static void
usage(void)
{
  dprintf(2, "usage: kernperf [-n runs]\n");
}

int
main(int argc, char *argv[])
{
  int i;
  int r;
  int runs;
  int nbench;
  int val;
  int run_score;
  int total_score;
  int total_max;
  int failures;
  int avg[16];

  runs = DEFAULT_RUNS;
  nbench = (int)(sizeof(benches) / sizeof(benches[0]));

  for(i = 1; i < argc; i++){
    if(strcmp(argv[i], "-h") == 0 || strcmp(argv[i], "--help") == 0){
      usage();
      exit(0);
    }
    if(strcmp(argv[i], "-n") == 0){
      if(i + 1 >= argc){
        usage();
        exit(1);
      }
      runs = atoi(argv[++i]);
      continue;
    }
    usage();
    exit(1);
  }

  if(runs < 1 || runs > MAX_RUNS){
    dprintf(2, "kernperf: runs must be between 1 and %d\n", MAX_RUNS);
    exit(1);
  }

  memset(avg, 0, sizeof(avg));
  total_score = 0;
  total_max = 0;
  failures = 0;

  dprintf(1, "kernperf: profile=%s runs=%d\n", KERNPERF_PROFILE, runs);

  for(r = 0; r < runs; r++){
    run_score = 0;
    for(i = 0; i < nbench; i++){
      val = 0;
      if(benches[i].run(&val) < 0){
        dprintf(1, "[FAIL] run=%d test=%s\n", r + 1, benches[i].name);
        failures++;
        continue;
      }

      avg[i] += val;
      {
        int pts = score_points(val, benches[i].target, benches[i].weight);
        run_score += pts;
        dprintf(1,
                "[PERF] run=%d test=%s value=%d %s target=%d score=%d/%d\n",
                r + 1,
                benches[i].name,
                val,
                benches[i].unit,
                benches[i].target,
                pts,
                benches[i].weight);
      }
    }

    total_score += run_score;
    total_max += 100;
    dprintf(1, "[RUN] %d score=%d/100\n", r + 1, run_score);
  }

  dprintf(1, "[SUMMARY] avg-score=%d/100\n", total_score / runs);
  for(i = 0; i < nbench; i++){
    int mean = avg[i] / runs;
    dprintf(1, "[SUMMARY] test=%s avg=%d %s target=%d\n",
            benches[i].name, mean, benches[i].unit, benches[i].target);
  }

  if(failures){
    dprintf(1, "kernperf: failures=%d\n", failures);
    exit(1);
  }

  exit(0);
}
