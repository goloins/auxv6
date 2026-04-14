// kallocstress.c - allocator-focused stress and regression checks
//
// Purpose:
// - Catch allocator correctness regressions (fork/copyuvm pressure)
// - Exercise page allocator churn through fork/exit and pipe activity
// - Provide a lightweight perf ruler specific to allocator-heavy paths

#include "types.h"
#include "sys/stat.h"
#include "auxv6/user.h"
#include "fcntl.h"
#include "param.h"

#define KALLOCSTRESS_PROFILE "2026-04-06-r4"
#define PAGE_BYTES 4096

#define PASS(name) do { dprintf(1, "[PASS] %s\n", name); passed++; } while(0)
#define FAIL(name, why) do { dprintf(1, "[FAIL] %s: %s\n", name, why); failed++; } while(0)

static int passed;
static int failed;
static int perf_score;
static int perf_score_max;

struct pipe_sys_timing {
  uint pipe_ticks;
  uint fork_ticks;
  uint read_ticks;
  uint wait_ticks;
  uint close_ticks;
  int pipe_calls;
  int fork_calls;
  int read_calls;
  int wait_calls;
  int close_calls;
};

struct fork_sys_timing {
  uint fork_ticks;
  uint wait_ticks;
  int fork_calls;
  int wait_calls;
};

struct vmstat_sample {
  int pages_free;
  int cache_alloc_hits;
  int cache_alloc_misses;
  int global_refill_batches;
  int global_refill_pages;
  int global_drain_batches;
  int global_drain_pages;
  int ref_increments;
  int deferred_frees;
  int vm_sync_calls;
  int vm_sync_full_calls;
  int vm_sync_entries;
  int vm_pde_repairs;
  int vm_master_repairs;
  int vm_bad_pte_drops;
  int pipe_read_sleeps;
  int pipe_write_sleeps;
  int pipe_wake_readers;
  int pipe_wake_writers;
};

struct schedstat_sample {
  int passes;
  int idle_halts;
  int picks;
  int wake_calls;
  int wake_scanned;
  int wake_matched;
  int waitpid_loops;
  int waitpid_scanned;
  int wake_ticks_calls;
  int wake_proc_calls;
  int wake_other_calls;
};

static void
vmstat_sample_init(struct vmstat_sample *s)
{
  memset(s, 0xff, sizeof(*s));
}

static void
print_pipe_sys_timing(struct pipe_sys_timing *tm)
{
  int pipe_avg_ms;
  int fork_avg_ms;
  int read_avg_ms;
  int wait_avg_ms;
  int close_avg_ms;

  pipe_avg_ms = tm->pipe_calls ? (int)((tm->pipe_ticks * 10U) / (uint)tm->pipe_calls) : 0;
  fork_avg_ms = tm->fork_calls ? (int)((tm->fork_ticks * 10U) / (uint)tm->fork_calls) : 0;
  read_avg_ms = tm->read_calls ? (int)((tm->read_ticks * 10U) / (uint)tm->read_calls) : 0;
  wait_avg_ms = tm->wait_calls ? (int)((tm->wait_ticks * 10U) / (uint)tm->wait_calls) : 0;
  close_avg_ms = tm->close_calls ? (int)((tm->close_ticks * 10U) / (uint)tm->close_calls) : 0;

  dprintf(1,
          "[DIAG] pipe-sys: pipe=%u/%d fork=%u/%d read=%u/%d waitpid=%u/%d close=%u/%d ticks avg_ms={%d,%d,%d,%d,%d}\n",
          tm->pipe_ticks, tm->pipe_calls,
          tm->fork_ticks, tm->fork_calls,
          tm->read_ticks, tm->read_calls,
          tm->wait_ticks, tm->wait_calls,
          tm->close_ticks, tm->close_calls,
          pipe_avg_ms, fork_avg_ms, read_avg_ms, wait_avg_ms, close_avg_ms);
}

static void
print_fork_sys_timing(const char *tag, struct fork_sys_timing *tm)
{
  int fork_avg_ms;
  int wait_avg_ms;

  fork_avg_ms = tm->fork_calls ? (int)((tm->fork_ticks * 10U) / (uint)tm->fork_calls) : 0;
  wait_avg_ms = tm->wait_calls ? (int)((tm->wait_ticks * 10U) / (uint)tm->wait_calls) : 0;

  dprintf(1,
          "[DIAG] %s-sys: fork=%u/%d waitpid=%u/%d ticks avg_ms={%d,%d}\n",
          tag,
          tm->fork_ticks, tm->fork_calls,
          tm->wait_ticks, tm->wait_calls,
          fork_avg_ms, wait_avg_ms);
}

static int
ops_per_sec(int ops, uint t0, uint t1)
{
  uint dt = (t1 > t0) ? (t1 - t0) : 1;
  return (int)((ops * 100U) / dt);
}

static void
perf_record(const char *name, const char *unit, int value, int target, int max_pts)
{
  int pts = 0;
  long long scaled;

  if(max_pts <= 0)
    max_pts = 1;

  if(target > 0){
    if(value >= target)
      pts = max_pts;
    else {
      // Rounded fixed-point score to avoid truncation bias.
      scaled = (long long)value * (long long)max_pts;
      pts = (int)((scaled + (target / 2)) / target);
    }
  }

  if(pts < 0) pts = 0;
  if(pts > max_pts) pts = max_pts;

  perf_score += pts;
  perf_score_max += max_pts;
  dprintf(1, "[PERF] %s: %d %s (target >= %d) score %d/%d\n",
          name, value, unit, target, pts, max_pts);
}

static int
read_text(const char *path, char *buf, int max)
{
  int fd, n, off;

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
find_kb_value(char *text, const char *key)
{
  int i, j;

  for(i = 0; text[i]; i++){
    for(j = 0; key[j] && text[i + j] == key[j]; j++)
      ;
    if(key[j] != 0)
      continue;

    i += j;
    while(text[i] && (text[i] < '0' || text[i] > '9'))
      i++;
    return atoi(&text[i]);
  }

  return -1;
}

static int
read_memfree_kb(void)
{
  char buf[256];
  if(read_text("/proc/meminfo", buf, sizeof(buf)) < 0)
    return -1;
  return find_kb_value(buf, "MemFree:");
}

static int
read_vmstat_sample(struct vmstat_sample *s)
{
  char buf[2048];

  vmstat_sample_init(s);
  if(read_text("/proc/vmstat", buf, sizeof(buf)) < 0)
    return -1;

  s->pages_free = find_kb_value(buf, "pages_free");
  s->cache_alloc_hits = find_kb_value(buf, "cache_alloc_hits");
  s->cache_alloc_misses = find_kb_value(buf, "cache_alloc_misses");
  s->global_refill_batches = find_kb_value(buf, "global_refill_batches");
  s->global_refill_pages = find_kb_value(buf, "global_refill_pages");
  s->global_drain_batches = find_kb_value(buf, "global_drain_batches");
  s->global_drain_pages = find_kb_value(buf, "global_drain_pages");
  s->ref_increments = find_kb_value(buf, "ref_increments");
  s->deferred_frees = find_kb_value(buf, "deferred_frees");
  s->vm_sync_calls = find_kb_value(buf, "vm_sync_calls");
  s->vm_sync_full_calls = find_kb_value(buf, "vm_sync_full_calls");
  s->vm_sync_entries = find_kb_value(buf, "vm_sync_entries");
  s->vm_pde_repairs = find_kb_value(buf, "vm_pde_repairs");
  s->vm_master_repairs = find_kb_value(buf, "vm_master_repairs");
  s->vm_bad_pte_drops = find_kb_value(buf, "vm_bad_pte_drops");
  s->pipe_read_sleeps = find_kb_value(buf, "pipe_read_sleeps");
  s->pipe_write_sleeps = find_kb_value(buf, "pipe_write_sleeps");
  s->pipe_wake_readers = find_kb_value(buf, "pipe_wake_readers");
  s->pipe_wake_writers = find_kb_value(buf, "pipe_wake_writers");
  return 0;
}

static int
delta_or_na(int after, int before)
{
  if(after < 0 || before < 0)
    return 0x7fffffff;
  return after - before;
}

static void
print_vmstat_delta(int run_idx, int nruns,
                   struct vmstat_sample *before,
                   struct vmstat_sample *after)
{
  int d_pages_free = delta_or_na(after->pages_free, before->pages_free);
  int d_hits = delta_or_na(after->cache_alloc_hits, before->cache_alloc_hits);
  int d_miss = delta_or_na(after->cache_alloc_misses, before->cache_alloc_misses);
  int d_refill_b = delta_or_na(after->global_refill_batches, before->global_refill_batches);
  int d_refill_p = delta_or_na(after->global_refill_pages, before->global_refill_pages);
  int d_drain_b = delta_or_na(after->global_drain_batches, before->global_drain_batches);
  int d_drain_p = delta_or_na(after->global_drain_pages, before->global_drain_pages);
  int d_ref_inc = delta_or_na(after->ref_increments, before->ref_increments);
  int d_def_free = delta_or_na(after->deferred_frees, before->deferred_frees);
    int d_vm_sync_calls = delta_or_na(after->vm_sync_calls, before->vm_sync_calls);
    int d_vm_sync_full = delta_or_na(after->vm_sync_full_calls, before->vm_sync_full_calls);
    int d_vm_sync_entries = delta_or_na(after->vm_sync_entries, before->vm_sync_entries);
    int d_vm_repairs = delta_or_na(after->vm_pde_repairs, before->vm_pde_repairs);
    int d_vm_master_repairs = delta_or_na(after->vm_master_repairs, before->vm_master_repairs);
    int d_vm_bad_pte = delta_or_na(after->vm_bad_pte_drops, before->vm_bad_pte_drops);
    int d_pipe_read_sleeps = delta_or_na(after->pipe_read_sleeps, before->pipe_read_sleeps);
    int d_pipe_write_sleeps = delta_or_na(after->pipe_write_sleeps, before->pipe_write_sleeps);
    int d_pipe_wake_readers = delta_or_na(after->pipe_wake_readers, before->pipe_wake_readers);
    int d_pipe_wake_writers = delta_or_na(after->pipe_wake_writers, before->pipe_wake_writers);

    dprintf(1,
      "[DIAG] run-vmstat %d/%d a: free=%d hits=%d miss=%d refill_b=%d refill_p=%d drain_b=%d drain_p=%d\n",
      run_idx, nruns,
      d_pages_free, d_hits, d_miss,
      d_refill_b, d_refill_p,
      d_drain_b, d_drain_p);
    dprintf(1,
      "[DIAG] run-vmstat %d/%d b: ref_inc=%d def_free=%d vm_sync=%d vm_sync_full=%d vm_sync_ent=%d vm_repairs=%d vm_master_repairs=%d vm_bad_pte=%d\n",
      run_idx, nruns,
      d_ref_inc, d_def_free,
      d_vm_sync_calls, d_vm_sync_full, d_vm_sync_entries,
      d_vm_repairs, d_vm_master_repairs, d_vm_bad_pte);
    dprintf(1,
      "[DIAG] run-vmstat %d/%d c: pipe_read_sleep=%d pipe_write_sleep=%d pipe_wake_r=%d pipe_wake_w=%d\n",
      run_idx, nruns,
      d_pipe_read_sleeps, d_pipe_write_sleeps,
      d_pipe_wake_readers, d_pipe_wake_writers);
}

static int
read_schedstat_sample(struct schedstat_sample *s)
{
  char buf[1024];

  memset(s, 0xff, sizeof(*s));
  if(read_text("/proc/schedstat", buf, sizeof(buf)) < 0)
    return -1;

  s->passes = find_kb_value(buf, "passes");
  s->idle_halts = find_kb_value(buf, "idle_halts");
  s->picks = find_kb_value(buf, "picks");
  s->wake_calls = find_kb_value(buf, "wake_calls");
  s->wake_scanned = find_kb_value(buf, "wake_scanned");
  s->wake_matched = find_kb_value(buf, "wake_matched");
  s->waitpid_loops = find_kb_value(buf, "waitpid_loops");
  s->waitpid_scanned = find_kb_value(buf, "waitpid_scanned");
  s->wake_ticks_calls = find_kb_value(buf, "wake_ticks_calls");
  s->wake_proc_calls = find_kb_value(buf, "wake_proc_calls");
  s->wake_other_calls = find_kb_value(buf, "wake_other_calls");
  return 0;
}

static void
print_schedstat_delta(int run_idx, int nruns,
                      struct schedstat_sample *before,
                      struct schedstat_sample *after)
{
  int d_passes = delta_or_na(after->passes, before->passes);
  int d_idle = delta_or_na(after->idle_halts, before->idle_halts);
  int d_picks = delta_or_na(after->picks, before->picks);
  int d_wake_calls = delta_or_na(after->wake_calls, before->wake_calls);
  int d_wake_scanned = delta_or_na(after->wake_scanned, before->wake_scanned);
  int d_wake_matched = delta_or_na(after->wake_matched, before->wake_matched);
  int d_wait_loops = delta_or_na(after->waitpid_loops, before->waitpid_loops);
  int d_wait_scanned = delta_or_na(after->waitpid_scanned, before->waitpid_scanned);
    int d_wake_ticks = delta_or_na(after->wake_ticks_calls, before->wake_ticks_calls);
    int d_wake_proc = delta_or_na(after->wake_proc_calls, before->wake_proc_calls);
    int d_wake_other = delta_or_na(after->wake_other_calls, before->wake_other_calls);

    dprintf(1,
      "[DIAG] run-sched %d/%d a: passes=%d idle=%d picks=%d wake_calls=%d wake_scanned=%d wake_matched=%d\n",
      run_idx, nruns,
      d_passes, d_idle, d_picks,
      d_wake_calls, d_wake_scanned, d_wake_matched);
    dprintf(1,
      "[DIAG] run-sched %d/%d b: wait_loops=%d wait_scanned=%d wake_ticks=%d wake_proc=%d wake_other=%d\n",
      run_idx, nruns,
      d_wait_loops, d_wait_scanned,
      d_wake_ticks, d_wake_proc, d_wake_other);
}

// ---------------------------------------------------------------------------
// T1: fork-copyuvm pressure
// Repeatedly fork children that grow/touch private memory then exit.
// This directly stresses setupkvm/copyuvm + allocator churn.
// ---------------------------------------------------------------------------
#define FORK_ROUNDS 120
#define FORK_CHILD_PAGES 8

static void
test_fork_copyuvm_pressure(void)
{
  int i;
  int ok = 1;
  uint t0 = uptime();
  uint ts;
  struct fork_sys_timing tm;

  memset(&tm, 0, sizeof(tm));

  for(i = 0; i < FORK_ROUNDS; i++){
    ts = uptime();
    int pid = fork();
    tm.fork_ticks += uptime() - ts;
    tm.fork_calls++;
    if(pid < 0){
      ok = 0;
      break;
    }
    if(pid == 0){
      char *base = sbrk(FORK_CHILD_PAGES * PAGE_BYTES);
      if(base == (char*)-1)
        exit(2);
      int p;
      for(p = 0; p < FORK_CHILD_PAGES; p++)
        base[p * PAGE_BYTES] = (char)(p + i);
      exit(0);
    }

    int st = 0;
    ts = uptime();
    if(waitpid(pid, &st, 0) != pid || st != 0){
      tm.wait_ticks += uptime() - ts;
      tm.wait_calls++;
      ok = 0;
      break;
    }
    tm.wait_ticks += uptime() - ts;
    tm.wait_calls++;
  }

  if(ok)
    PASS("fork-copyuvm-pressure");
  else
    FAIL("fork-copyuvm-pressure", "fork/waitpid/child status failure");

  perf_record("fork-copyuvm", "fork/s",
              ops_per_sec(FORK_ROUNDS, t0, uptime()),
              180,
              40);

  print_fork_sys_timing("fork", &tm);
}

// ---------------------------------------------------------------------------
// T2: pipe-page churn
// Rapid pipe create/write/read/close cycles exercise allocator paths used by
// pipe buffers and process wakeup interactions.
// ---------------------------------------------------------------------------
#define PIPE_ROUNDS 200
#define PIPE_MSG_SIZE 256

static void
test_pipe_page_churn(void)
{
  int i;
  int ok = 1;
  char wbuf[PIPE_MSG_SIZE];
  char rbuf[PIPE_MSG_SIZE];
  uint t0 = uptime();
  uint ts;
  struct pipe_sys_timing tm;

  memset(&tm, 0, sizeof(tm));

  for(i = 0; i < PIPE_MSG_SIZE; i++)
    wbuf[i] = (char)(i & 0x7f);

  for(i = 0; i < PIPE_ROUNDS; i++){
    int pfd[2];
    ts = uptime();
    if(pipe(pfd) < 0){
      tm.pipe_ticks += uptime() - ts;
      tm.pipe_calls++;
      ok = 0;
      break;
    }
    tm.pipe_ticks += uptime() - ts;
    tm.pipe_calls++;

    ts = uptime();
    int pid = fork();
    tm.fork_ticks += uptime() - ts;
    tm.fork_calls++;
    if(pid < 0){
      ts = uptime();
      close(pfd[0]);
      tm.close_ticks += uptime() - ts;
      tm.close_calls++;
      ts = uptime();
      close(pfd[1]);
      tm.close_ticks += uptime() - ts;
      tm.close_calls++;
      ok = 0;
      break;
    }

    if(pid == 0){
      close(pfd[0]);
      if(write(pfd[1], wbuf, sizeof(wbuf)) != sizeof(wbuf)){
        close(pfd[1]);
        exit(3);
      }
      close(pfd[1]);
      exit(0);
    }

    ts = uptime();
    close(pfd[1]);
    tm.close_ticks += uptime() - ts;
    tm.close_calls++;

    ts = uptime();
    if(read(pfd[0], rbuf, sizeof(rbuf)) != sizeof(rbuf))
      ok = 0;
    tm.read_ticks += uptime() - ts;
    tm.read_calls++;

    ts = uptime();
    close(pfd[0]);
    tm.close_ticks += uptime() - ts;
    tm.close_calls++;

    int st = 0;
    ts = uptime();
    if(waitpid(pid, &st, 0) != pid || st != 0)
      ok = 0;
    tm.wait_ticks += uptime() - ts;
    tm.wait_calls++;

    if(!ok)
      break;
  }

  if(ok)
    PASS("pipe-page-churn");
  else
    FAIL("pipe-page-churn", "pipe/read/write/waitpid failure");

  perf_record("pipe-page-churn", "round/s",
              ops_per_sec(PIPE_ROUNDS, t0, uptime()),
              320,
              35);

  print_pipe_sys_timing(&tm);
}

// ---------------------------------------------------------------------------
// T3: allocator reclaim sanity
// Compare MemFree before/after heavy allocator activity and fail only on a
// large sustained drop to catch leak-style regressions.
// ---------------------------------------------------------------------------
#define LEAK_CHECK_FORKS 90
#define LEAK_CHILD_PAGES 6
#define LEAK_DROP_KB_MAX 16384

static void
test_allocator_reclaim_sanity(void)
{
  int before, after;
  int i;
  int ok = 1;
  uint t0 = uptime();
  uint ts;
  struct fork_sys_timing tm;

  memset(&tm, 0, sizeof(tm));

  before = read_memfree_kb();
  if(before < 0){
    FAIL("allocator-reclaim", "cannot read /proc/meminfo");
    return;
  }

  for(i = 0; i < LEAK_CHECK_FORKS; i++){
    ts = uptime();
    int pid = fork();
    tm.fork_ticks += uptime() - ts;
    tm.fork_calls++;
    if(pid < 0){
      ok = 0;
      break;
    }
    if(pid == 0){
      char *base = sbrk(LEAK_CHILD_PAGES * PAGE_BYTES);
      if(base == (char*)-1)
        exit(4);
      int p;
      for(p = 0; p < LEAK_CHILD_PAGES; p++)
        base[p * PAGE_BYTES] = (char)(p + 1);
      exit(0);
    }

    int st = 0;
    ts = uptime();
    if(waitpid(pid, &st, 0) != pid || st != 0){
      tm.wait_ticks += uptime() - ts;
      tm.wait_calls++;
      ok = 0;
      break;
    }
    tm.wait_ticks += uptime() - ts;
    tm.wait_calls++;
  }

  after = read_memfree_kb();
  if(after < 0){
    FAIL("allocator-reclaim", "cannot reread /proc/meminfo");
    return;
  }

  int drop = before - after;
  if(drop < 0)
    drop = 0;

  if(ok && drop <= LEAK_DROP_KB_MAX)
    PASS("allocator-reclaim");
  else {
    dprintf(1, "[FAIL] allocator-reclaim: MemFree drop %d KB (limit %d KB)\n",
            drop, LEAK_DROP_KB_MAX);
    failed++;
  }

  perf_record("allocator-reclaim", "fork/s",
              ops_per_sec(LEAK_CHECK_FORKS, t0, uptime()),
              160,
              25);

  dprintf(1, "[DIAG] reclaim-mem: before=%dKB after=%dKB drop=%dKB\n", before, after, drop);
  print_fork_sys_timing("reclaim", &tm);
}

#define MAX_RUNS 32

int
main(int argc, char *argv[])
{
  int nruns = 1;
  int r;

  if(argc == 3 && argv[1][0] == '-' && argv[1][1] == 'n' && argv[1][2] == '\0'){
    nruns = atoi(argv[2]);
    if(nruns < 1 || nruns > MAX_RUNS){
      dprintf(2, "usage: kallocstress [-n runs]\n");
      exit(1);
    }
  } else if(argc != 1){
    dprintf(2, "usage: kallocstress [-n runs]\n");
    exit(1);
  }

  int run_scores[MAX_RUNS];
  struct vmstat_sample vm_before;
  struct vmstat_sample vm_after;
  struct schedstat_sample sc_before;
  struct schedstat_sample sc_after;
  int total_passed = 0, total_failed_runs = 0;

  dprintf(1, "kallocstress: allocator-focused stress and regression checks\n");
  dprintf(1, "  NPROC=%d NCPU=%d NOFILE_HARD=%d\n", NPROC, NCPU, NOFILE_HARD);
  dprintf(1, "  profile=%s\n", KALLOCSTRESS_PROFILE);

  for(r = 0; r < nruns; r++){
    passed = failed = perf_score = perf_score_max = 0;
    if(nruns > 1)
      dprintf(1, "\n--- run %d/%d ---\n", r + 1, nruns);
    else
      dprintf(1, "\n");

    if(read_vmstat_sample(&vm_before) < 0)
      vmstat_sample_init(&vm_before);
    if(read_schedstat_sample(&sc_before) < 0)
      memset(&sc_before, 0xff, sizeof(sc_before));

    test_fork_copyuvm_pressure();
    test_pipe_page_churn();
    test_allocator_reclaim_sanity();

    if(read_vmstat_sample(&vm_after) < 0)
      vmstat_sample_init(&vm_after);
    if(read_schedstat_sample(&sc_after) < 0)
      memset(&sc_after, 0xff, sizeof(sc_after));

    run_scores[r] = perf_score;
    total_passed += passed;
    total_failed_runs += failed;

    dprintf(1, "\nkallocstress score: %d/%d (target >= 75)\n",
            perf_score, perf_score_max);
    dprintf(1, "kallocstress results: %d passed, %d failed\n", passed, failed);
        print_vmstat_delta(r + 1, nruns, &vm_before, &vm_after);
    print_schedstat_delta(r + 1, nruns, &sc_before, &sc_after);
  }

  if(nruns > 1){
    int sum = 0, mn = 101, mx = -1;
    for(r = 0; r < nruns; r++){
      if(run_scores[r] < mn) mn = run_scores[r];
      if(run_scores[r] > mx) mx = run_scores[r];
      sum += run_scores[r];
    }
    dprintf(1, "\n--- %d-run summary ---\n", nruns);
    dprintf(1, "  avg: %d/100  min: %d  max: %d\n", sum / nruns, mn, mx);
    dprintf(1, "  total: %d passed, %d failed\n", total_passed, total_failed_runs);
  }

  exit(total_failed_runs ? 1 : 0);
}
