// vmprobe.c - targeted VM/scheduler slowdown hypothesis probe
//
// This utility runs controlled micro-phases and correlates throughput with
// kernel counters from /proc/vmstat and /proc/schedstat.
//
// Goals:
// - Quantify fork/switch-path overhead and its drift across rounds.
// - Detect tick-sleeper fanout scaling regressions.
// - Surface whether VM PDE sync/repair counters track performance decline.

#include "types.h"
#include "sys/stat.h"
#include "fcntl.h"
#include "auxv6/user.h"

#define VMPROBE_PROFILE "2026-04-06-r1"

#define DEFAULT_ROUNDS 8
#define DEFAULT_FORKS 96
#define DEFAULT_SLEEPERS 8
#define DEFAULT_SLEEP_ITERS 24

#define MAX_ROUNDS 64

struct vmstat_sample {
  int vm_sync_calls;
  int vm_sync_full_calls;
  int vm_sync_entries;
  int vm_pde_repairs;
  int vm_master_repairs;
  int vm_bad_pte_drops;
};

struct schedstat_sample {
  int wake_calls;
  int wake_scanned;
  int wake_matched;
  int wake_ticks_calls;
  int wake_proc_calls;
  int wake_other_calls;
};

static void
sample_init(void *p, int n)
{
  memset(p, 0xff, n);
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
read_vmstat(struct vmstat_sample *s)
{
  char buf[2048];

  sample_init(s, sizeof(*s));
  if(read_text("/proc/vmstat", buf, sizeof(buf)) < 0)
    return -1;

  s->vm_sync_calls = find_kv_value(buf, "vm_sync_calls");
  s->vm_sync_full_calls = find_kv_value(buf, "vm_sync_full_calls");
  s->vm_sync_entries = find_kv_value(buf, "vm_sync_entries");
  s->vm_pde_repairs = find_kv_value(buf, "vm_pde_repairs");
  s->vm_master_repairs = find_kv_value(buf, "vm_master_repairs");
  s->vm_bad_pte_drops = find_kv_value(buf, "vm_bad_pte_drops");
  return 0;
}

static int
read_schedstat(struct schedstat_sample *s)
{
  char buf[1024];

  sample_init(s, sizeof(*s));
  if(read_text("/proc/schedstat", buf, sizeof(buf)) < 0)
    return -1;

  s->wake_calls = find_kv_value(buf, "wake_calls");
  s->wake_scanned = find_kv_value(buf, "wake_scanned");
  s->wake_matched = find_kv_value(buf, "wake_matched");
  s->wake_ticks_calls = find_kv_value(buf, "wake_ticks_calls");
  s->wake_proc_calls = find_kv_value(buf, "wake_proc_calls");
  s->wake_other_calls = find_kv_value(buf, "wake_other_calls");
  return 0;
}

static int
delta_or_na(int after, int before)
{
  if(after < 0 || before < 0)
    return -1;
  return after - before;
}

static int
bench_fork_wait(int forks, uint *dt_ticks)
{
  int i;
  int pid;
  uint t0;
  uint t1;

  t0 = uptime();
  for(i = 0; i < forks; i++){
    pid = fork();
    if(pid < 0)
      return -1;
    if(pid == 0)
      exit(0);
    int status;
    if(wait(&status) < 0)
      return -1;
  }
  t1 = uptime();

  *dt_ticks = (t1 > t0) ? (t1 - t0) : 1;
  return 0;
}

static int
bench_tick_sleepers(int sleepers, int iters, uint *dt_ticks)
{
  int i;
  int j;
  int pid;
  int st;
  uint t0;
  uint t1;

  t0 = uptime();
  for(i = 0; i < sleepers; i++){
    pid = fork();
    if(pid < 0)
      return -1;
    if(pid == 0){
      for(j = 0; j < iters; j++)
        sleep(1);
      exit(0);
    }
  }

  for(i = 0; i < sleepers; i++){
    int status;
    st = wait(&status);
    if(st < 0)
      return -1;
  }

  t1 = uptime();
  *dt_ticks = (t1 > t0) ? (t1 - t0) : 1;
  return 0;
}

static void
usage(void)
{
  dprintf(2, "usage: vmprobe [-r rounds] [-f forks] [-s sleepers] [-i sleep_iters]\n");
}

int
main(int argc, char *argv[])
{
  int i;
  int rounds;
  int forks;
  int sleepers;
  int sleep_iters;
  int first_fork_ops;
  int last_fork_ops;

  rounds = DEFAULT_ROUNDS;
  forks = DEFAULT_FORKS;
  sleepers = DEFAULT_SLEEPERS;
  sleep_iters = DEFAULT_SLEEP_ITERS;

  for(i = 1; i < argc; i++){
    if(strcmp(argv[i], "-h") == 0 || strcmp(argv[i], "--help") == 0){
      usage();
      exit(0);
    }
    if(strcmp(argv[i], "-r") == 0 && i + 1 < argc){
      rounds = atoi(argv[++i]);
      continue;
    }
    if(strcmp(argv[i], "-f") == 0 && i + 1 < argc){
      forks = atoi(argv[++i]);
      continue;
    }
    if(strcmp(argv[i], "-s") == 0 && i + 1 < argc){
      sleepers = atoi(argv[++i]);
      continue;
    }
    if(strcmp(argv[i], "-i") == 0 && i + 1 < argc){
      sleep_iters = atoi(argv[++i]);
      continue;
    }
    usage();
    exit(1);
  }

  if(rounds < 1 || rounds > MAX_ROUNDS || forks < 1 || sleepers < 1 || sleep_iters < 1){
    dprintf(2, "vmprobe: invalid arguments\n");
    exit(1);
  }

  first_fork_ops = -1;
  last_fork_ops = -1;

  dprintf(1,
          "vmprobe: profile=%s rounds=%d forks=%d sleepers=%d sleep_iters=%d\n",
          VMPROBE_PROFILE, rounds, forks, sleepers, sleep_iters);

  for(i = 0; i < rounds; i++){
    struct vmstat_sample vm0;
    struct vmstat_sample vm1;
    struct vmstat_sample vm2;
    struct vmstat_sample vm3;
    struct schedstat_sample sc0;
    struct schedstat_sample sc1;
    struct schedstat_sample sc2;
    struct schedstat_sample sc3;
    uint dt_fork;
    uint dt_tick_low;
    uint dt_tick_high;
    int fork_ops;
    int vm_sync_entries_d;
    int vm_sync_calls_d;
    int vm_pde_repairs_d;
    int vm_master_repairs_d;
    int vm_bad_pte_drops_d;
    int wake_scanned_low;
    int wake_scanned_high;
    int wake_ticks_low;
    int wake_ticks_high;
    int low_scale_pct;
    int high_scale_pct;

    if(read_vmstat(&vm0) < 0 || read_schedstat(&sc0) < 0){
      dprintf(1, "[WARN] round=%d pre-sample failed\n", i + 1);
      continue;
    }

    if(bench_fork_wait(forks, &dt_fork) < 0){
      dprintf(1, "[FAIL] round=%d fork-wait phase failed\n", i + 1);
      continue;
    }

    if(read_vmstat(&vm1) < 0 || read_schedstat(&sc1) < 0){
      dprintf(1, "[WARN] round=%d mid-sample(fork) failed\n", i + 1);
      continue;
    }

    if(bench_tick_sleepers(sleepers, sleep_iters, &dt_tick_low) < 0){
      dprintf(1, "[FAIL] round=%d tick-low phase failed\n", i + 1);
      continue;
    }

    if(read_vmstat(&vm2) < 0 || read_schedstat(&sc2) < 0){
      dprintf(1, "[WARN] round=%d mid-sample(tick-low) failed\n", i + 1);
      continue;
    }

    if(bench_tick_sleepers(sleepers * 2, sleep_iters, &dt_tick_high) < 0){
      dprintf(1, "[FAIL] round=%d tick-high phase failed\n", i + 1);
      continue;
    }

    if(read_vmstat(&vm3) < 0 || read_schedstat(&sc3) < 0){
      dprintf(1, "[WARN] round=%d post-sample failed\n", i + 1);
      continue;
    }

    fork_ops = (int)((forks * 100U) / dt_fork);
    if(first_fork_ops < 0)
      first_fork_ops = fork_ops;
    last_fork_ops = fork_ops;

    vm_sync_entries_d = delta_or_na(vm1.vm_sync_entries, vm0.vm_sync_entries);
    vm_sync_calls_d = delta_or_na(vm1.vm_sync_calls, vm0.vm_sync_calls);
    vm_pde_repairs_d = delta_or_na(vm1.vm_pde_repairs, vm0.vm_pde_repairs);
    vm_master_repairs_d = delta_or_na(vm1.vm_master_repairs, vm0.vm_master_repairs);
    vm_bad_pte_drops_d = delta_or_na(vm1.vm_bad_pte_drops, vm0.vm_bad_pte_drops);

    wake_scanned_low = delta_or_na(sc2.wake_scanned, sc1.wake_scanned);
    wake_scanned_high = delta_or_na(sc3.wake_scanned, sc2.wake_scanned);
    wake_ticks_low = delta_or_na(sc2.wake_ticks_calls, sc1.wake_ticks_calls);
    wake_ticks_high = delta_or_na(sc3.wake_ticks_calls, sc2.wake_ticks_calls);

    low_scale_pct = (int)((dt_tick_low * 100U) / (uint)sleep_iters);
    high_scale_pct = (int)((dt_tick_high * 100U) / (uint)sleep_iters);

    dprintf(1,
            "[ROUND %d] fork_ops=%d/s forks=%d dt_ticks=%u vm_sync_calls=%d vm_sync_entries=%d entries_per_fork=%d\n",
            i + 1,
            fork_ops,
            forks,
            dt_fork,
            vm_sync_calls_d,
            vm_sync_entries_d,
            (vm_sync_entries_d >= 0) ? (vm_sync_entries_d / forks) : -1);

    dprintf(1,
            "[ROUND %d] vm_repairs pde=%d master=%d bad_pte_drops=%d\n",
            i + 1,
            vm_pde_repairs_d,
            vm_master_repairs_d,
            vm_bad_pte_drops_d);

    dprintf(1,
            "[ROUND %d] tick_low sleepers=%d dt_ticks=%u scale=%d%% wake_ticks=%d wake_scanned=%d\n",
            i + 1,
            sleepers,
            dt_tick_low,
            low_scale_pct,
            wake_ticks_low,
            wake_scanned_low);

    dprintf(1,
            "[ROUND %d] tick_high sleepers=%d dt_ticks=%u scale=%d%% wake_ticks=%d wake_scanned=%d\n",
            i + 1,
            sleepers * 2,
            dt_tick_high,
            high_scale_pct,
            wake_ticks_high,
            wake_scanned_high);

    dprintf(1,
            "[ROUND %d] tick_scale_ratio=%d%% (high_vs_low)\n",
            i + 1,
            (int)((dt_tick_high * 100U) / dt_tick_low));
  }

  if(first_fork_ops > 0 && last_fork_ops > 0){
    int slope_pct;

    slope_pct = (last_fork_ops * 100) / first_fork_ops;
    dprintf(1,
            "[SUMMARY] fork_ops first=%d/s last=%d/s slope=%d%%\n",
            first_fork_ops,
            last_fork_ops,
            slope_pct);

    if(slope_pct < 85)
      dprintf(1, "[HYPOTHESIS] long-run decline confirmed in fork/switch path\n");
    else
      dprintf(1, "[HYPOTHESIS] no strong fork/switch decline over this window\n");
  }

  dprintf(1, "[NOTE] correlate rounds with kallocstress decline timeline for confidence\n");
  exit(0);
}
