// kallocstress.c - allocator-focused stress and regression checks
//
// Purpose:
// - Catch allocator correctness regressions (fork/copyuvm pressure)
// - Exercise page allocator churn through fork/exit and pipe activity
// - Provide a lightweight perf ruler specific to allocator-heavy paths

#include "types.h"
#include "stat.h"
#include "auxv6/user.h"
#include "fcntl.h"
#include "param.h"

#define KALLOCSTRESS_PROFILE "2026-04-03-r2"
#define PAGE_BYTES 4096

#define PASS(name) do { dprintf(1, "[PASS] %s\n", name); passed++; } while(0)
#define FAIL(name, why) do { dprintf(1, "[FAIL] %s: %s\n", name, why); failed++; } while(0)

static int passed;
static int failed;
static int perf_score;
static int perf_score_max;

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

  if(target > 0){
    if(value >= target)
      pts = max_pts;
    else
      pts = (value * max_pts) / target;
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

  for(i = 0; i < FORK_ROUNDS; i++){
    int pid = fork();
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
    if(waitpid(pid, &st, 0) != pid || st != 0){
      ok = 0;
      break;
    }
  }

  if(ok)
    PASS("fork-copyuvm-pressure");
  else
    FAIL("fork-copyuvm-pressure", "fork/waitpid/child status failure");

  perf_record("fork-copyuvm", "fork/s",
              ops_per_sec(FORK_ROUNDS, t0, uptime()),
              180,
              40);
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

  for(i = 0; i < PIPE_MSG_SIZE; i++)
    wbuf[i] = (char)(i & 0x7f);

  for(i = 0; i < PIPE_ROUNDS; i++){
    int pfd[2];
    if(pipe(pfd) < 0){
      ok = 0;
      break;
    }

    int pid = fork();
    if(pid < 0){
      close(pfd[0]);
      close(pfd[1]);
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

    close(pfd[1]);
    if(read(pfd[0], rbuf, sizeof(rbuf)) != sizeof(rbuf))
      ok = 0;
    close(pfd[0]);

    int st = 0;
    if(waitpid(pid, &st, 0) != pid || st != 0)
      ok = 0;

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

  before = read_memfree_kb();
  if(before < 0){
    FAIL("allocator-reclaim", "cannot read /proc/meminfo");
    return;
  }

  for(i = 0; i < LEAK_CHECK_FORKS; i++){
    int pid = fork();
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
    if(waitpid(pid, &st, 0) != pid || st != 0){
      ok = 0;
      break;
    }
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

    test_fork_copyuvm_pressure();
    test_pipe_page_churn();
    test_allocator_reclaim_sanity();

    run_scores[r] = perf_score;
    total_passed += passed;
    total_failed_runs += failed;

    dprintf(1, "\nkallocstress score: %d/%d (target >= 75)\n",
            perf_score, perf_score_max);
    dprintf(1, "kallocstress results: %d passed, %d failed\n", passed, failed);
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
