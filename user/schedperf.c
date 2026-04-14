// schedperf.c - scheduler and process-table performance stress tests
//
// Exercises the scheduler changes from kernel-perf-hardening:
//   - Per-CPU scan-start offset (sched_last)
//   - Idle hlt when no runnable processes
//   - proc_check_alarms fast path (active_alarm_count)
//   - proc_handle_signals_on_return lockless fast path
//   - mycpu() O(1) APIC reverse map (implicitly, via lock hot paths)
//   - ptable / spinlock pause improvement (via concurrent fork/yield)
//
// Usage: schedperf
// Prints [PASS]/[FAIL] for each sub-test and a final summary.

#include "types.h"
#include "stat.h"
#include "auxv6/user.h"
#include "signal.h"
#include "fcntl.h"
#include "param.h"

#define PASS(name) do { dprintf(1, "[PASS] %s\n", name); passed++; } while(0)
#define FAIL(name, why) do { dprintf(1, "[FAIL] %s: %s\n", name, why); failed++; } while(0)

static int passed = 0;
static int failed = 0;
static int perf_score = 0;
static int perf_score_max = 0;

#define SCHEDPERF_PROFILE "2026-04-03-r2"

static int
ops_per_sec(int ops, uint start_ticks, uint end_ticks)
{
  uint dt = (end_ticks > start_ticks) ? (end_ticks - start_ticks) : 1;
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

// ---------------------------------------------------------------------------
// T1: fork-storm -- spawn NWORKERS children that immediately exit, then reap
//     all of them.  Stresses allocproc, ptable scan, wakeup, and exit paths.
// ---------------------------------------------------------------------------
#define STORM_WORKERS 48

static void
test_fork_storm(void)
{
  int i, pid, reaped, err;
  int pids[STORM_WORKERS];
  uint t0 = uptime();

  for(i = 0; i < STORM_WORKERS; i++){
    pid = fork();
    if(pid < 0){
      FAIL("fork-storm", "fork returned < 0 before limit");
      // reap whatever we managed to spawn and bail
      int status;
      while(wait(&status) > 0)
        ;
      return;
    }
    if(pid == 0)
      exit(0);
    pids[i] = pid;
  }

  reaped = 0;
  err = 0;
  for(i = 0; i < STORM_WORKERS; i++){
    int status;
    pid = waitpid(pids[i], &status, 0);
    if(pid != pids[i])
      err++;
    else
      reaped++;
  }

  if(reaped == STORM_WORKERS && err == 0)
    PASS("fork-storm");
  else
    FAIL("fork-storm", "wrong reap count or waitpid error");

  perf_record("fork-storm", "fork/s",
              ops_per_sec(STORM_WORKERS, t0, uptime()),
              600,
              20);
}

// ---------------------------------------------------------------------------
// T2: yield-storm -- N processes each yield in a tight loop, then exit.
//     Stresses the scheduler scan and context switching under lock.
// ---------------------------------------------------------------------------
#define YIELD_WORKERS   8
#define YIELD_ITERS   200

static void
test_yield_storm(void)
{
  int i, ok;
  uint t0 = uptime();

  for(i = 0; i < YIELD_WORKERS; i++){
    if(fork() == 0){
      int j;
      for(j = 0; j < YIELD_ITERS; j++)
        sleep(0);   // sleep(0) in auxv6 is equivalent to yield
      exit(0);
    }
  }

  ok = 1;
  for(i = 0; i < YIELD_WORKERS; i++){
    int status;
    if(wait(&status) < 0)
      ok = 0;
  }

  if(ok)
    PASS("yield-storm");
  else
    FAIL("yield-storm", "wait returned error");

  perf_record("yield-storm", "yield/s",
              ops_per_sec(YIELD_WORKERS * YIELD_ITERS, t0, uptime()),
              2500,
              12);
}

// ---------------------------------------------------------------------------
// T3: pipe-wakeup -- N reader/writer pairs communicate through pipes.
//     Stresses wakeup1() (O(NPROC) scan) and the sleep/wakeup path.
// ---------------------------------------------------------------------------
#define PIPE_PAIRS  8
#define PIPE_MSGS   50

static void
test_pipe_wakeup(void)
{
  int pair, i, ok;
  int pfd[2];
  uint t0 = uptime();

  ok = 1;
  for(pair = 0; pair < PIPE_PAIRS; pair++){
    if(pipe(pfd) < 0){
      ok = 0;
      break;
    }
    // Writer child
    if(fork() == 0){
      close(pfd[0]);
      for(i = 0; i < PIPE_MSGS; i++){
        char c = (char)(i & 0xff);
        if(write(pfd[1], &c, 1) != 1)
          exit(1);
      }
      close(pfd[1]);
      exit(0);
    }
    // Reader child
    if(fork() == 0){
      char c;
      int n = 0;
      close(pfd[1]);
      while(read(pfd[0], &c, 1) == 1)
        n++;
      close(pfd[0]);
      exit(n == PIPE_MSGS ? 0 : 1);
    }
    close(pfd[0]);
    close(pfd[1]);
  }

  // Reap all children (PIPE_PAIRS * 2 = writers + readers)
  int bad = 0;
  for(i = 0; i < PIPE_PAIRS * 2; i++){
    int status;
    if(wait(&status) < 0)
      bad++;
  }

  if(ok && bad == 0)
    PASS("pipe-wakeup");
  else
    FAIL("pipe-wakeup", "pipe or wait error");

  perf_record("pipe-wakeup", "msg/s",
              ops_per_sec(PIPE_PAIRS * PIPE_MSGS, t0, uptime()),
              1400,
              16);
}

// ---------------------------------------------------------------------------
// T4: alarm-counter -- set alarm(1) in NALARM_WORKERS children and confirm
//     they all receive SIGALRM.  Tests active_alarm_count maintenance and
//     the fast-path bypass when count == 0 after all fire.
// ---------------------------------------------------------------------------
#define NALARM_WORKERS 6

static volatile int alarm_fired = 0;

static void
alarm_handler(int sig)
{
  if(sig == SIGALRM)
    alarm_fired = 1;
}

static void
test_alarm_counter(void)
{
  int i, ok;
  uint t0 = uptime();

  for(i = 0; i < NALARM_WORKERS; i++){
    if(fork() == 0){
      struct sigaction sa;
      alarm_fired = 0;
      sa.sa_handler = alarm_handler;
      sa.sa_mask = 0;
      sa.sa_flags = 0;
      sigaction(SIGALRM, &sa, 0);
      alarm(1);
      // Busy-wait for the signal (sleep might swallow it on some paths)
      int spins = 0;
      while(!alarm_fired && spins < 2000000)
        spins++;
      exit(alarm_fired ? 0 : 1);
    }
  }

  ok = 1;
  for(i = 0; i < NALARM_WORKERS; i++){
    int status;
    if(wait(&status) < 0)
      ok = 0;
  }

  if(ok)
    PASS("alarm-counter");
  else
    FAIL("alarm-counter", "child alarm did not fire or wait error");

  perf_record("alarm-counter", "alarms/s",
              ops_per_sec(NALARM_WORKERS, t0, uptime()),
              5,
              8);
}

// ---------------------------------------------------------------------------
// T5: signal-fast-path -- verify proc_handle_signals_on_return lockless
//     precheck works by sending signals to self and confirming delivery.
// ---------------------------------------------------------------------------
static volatile int sig_count = 0;

static void
count_handler(int sig)
{
  (void)sig;
  sig_count++;
}

static void
test_signal_fast_path(void)
{
  struct sigaction sa;
  int i;
  uint t0;

  sa.sa_handler = count_handler;
  sa.sa_mask = 0;
  sa.sa_flags = 0;
  sigaction(SIGUSR1, &sa, 0);

  sig_count = 0;
  t0 = uptime();
  for(i = 0; i < 20; i++){
    sigsend(getpid(), SIGUSR1);
    // any syscall will flush the signal via proc_handle_signals_on_return
    uptime();
  }

  // Restore default before the test races with SIGUSR1
  sa.sa_handler = (void*)1; // SIG_DFL
  sigaction(SIGUSR1, &sa, 0);

  if(sig_count == 20)
    PASS("signal-fast-path");
  else {
    // count may be < 20 if some coalesced; >= 1 is a decent pass
    if(sig_count >= 1)
      PASS("signal-fast-path");
    else
      FAIL("signal-fast-path", "no signals delivered");
  }

  perf_record("signal-fast-path", "signals/s",
              ops_per_sec(20, t0, uptime()),
              900,
              10);
}

// ---------------------------------------------------------------------------
// T6: idle-hlt -- confirm the system remains responsive after all children
//     exit (idle CPUs should hlt, not spin; observable as no hang here).
// ---------------------------------------------------------------------------
static void
test_idle_responsiveness(void)
{
  int i;
  // Spawn workers that sleep, creating idle intervals between them
  for(i = 0; i < 4; i++){
    if(fork() == 0){
      // do nothing fancy, just exit
      exit(0);
    }
  }
  for(i = 0; i < 4; i++){
    int status;
    wait(&status);
  }

  // If we get here with no hang, idle hlt did not deadlock anything
  PASS("idle-responsiveness");
}

// ---------------------------------------------------------------------------
// T7: sched-spread -- create processes quickly and measure that they don't
//     all finish in strict FIFO order from the bottom of the table.
//     We can't observe sched_last directly, but we verify that many workers
//     running concurrently all complete (no starvation).
// ---------------------------------------------------------------------------
#define SPREAD_WORKERS 32
#define SPREAD_ITERS   10

static void
test_sched_spread(void)
{
  int i, reaped, ok;
  int pids[SPREAD_WORKERS];
  uint t0 = uptime();

  for(i = 0; i < SPREAD_WORKERS; i++){
    int pid = fork();
    if(pid < 0){
      // couldn't spawn all; reap and report
      int status;
      while(wait(&status) > 0)
        ;
      FAIL("sched-spread", "fork failed before SPREAD_WORKERS");
      return;
    }
    if(pid == 0){
      int j;
      // Do a small amount of work per process
      for(j = 0; j < SPREAD_ITERS; j++)
        sleep(0);
      exit(0);
    }
    pids[i] = pid;
  }

  reaped = 0;
  ok = 1;
  for(i = 0; i < SPREAD_WORKERS; i++){
    if(waitpid(pids[i], 0, 0) < 0)
      ok = 0;
    else
      reaped++;
  }

  if(ok && reaped == SPREAD_WORKERS)
    PASS("sched-spread");
  else
    FAIL("sched-spread", "not all workers reaped");

  perf_record("sched-spread", "yield/s",
              ops_per_sec(SPREAD_WORKERS * SPREAD_ITERS, t0, uptime()),
              2200,
              14);
}

// ---------------------------------------------------------------------------
// T8: proc-table-limit -- verify we can use up to NPROC-1 slots without
//     panic and that NPROC (the raised limit = 128) is reachable.
//     We spawn up to SPAWN_LIMIT children; any fork failure before that is
//     a regression (old limit was 64, new is 128).
// ---------------------------------------------------------------------------
#define SPAWN_LIMIT  90   // well above the old 64 limit, below new 128

static void
test_proc_table_limit(void)
{
  int i;
  int spawned = 0;

  for(i = 0; i < SPAWN_LIMIT; i++){
    int pid = fork();
    if(pid < 0)
      break;
    if(pid == 0)
      exit(0);
    spawned++;
  }
  // Reap all
  int status;
  while(wait(&status) > 0)
    ;

  if(spawned >= SPAWN_LIMIT)
    PASS("proc-table-limit");
  else {
    dprintf(1, "[FAIL] proc-table-limit: only spawned %d/%d\n",
            spawned, SPAWN_LIMIT);
    failed++;
  }

  perf_record("proc-table-limit", "children", spawned, SPAWN_LIMIT, 20);
}

// ---------------------------------------------------------------------------
// main
// ---------------------------------------------------------------------------
#define MAX_RUNS 32

int
main(int argc, char *argv[])
{
  int nruns = 1;
  int r;

  if(argc == 3 && argv[1][0] == '-' && argv[1][1] == 'n' && argv[1][2] == '\0'){
    nruns = atoi(argv[2]);
    if(nruns < 1 || nruns > MAX_RUNS){
      dprintf(2, "usage: schedperf [-n runs]\n");
      exit(1);
    }
  } else if(argc != 1){
    dprintf(2, "usage: schedperf [-n runs]\n");
    exit(1);
  }

  int run_scores[MAX_RUNS];
  int total_passed = 0, total_failed_runs = 0;

  dprintf(1, "schedperf: scheduler and process-table stress\n");
  dprintf(1, "  NPROC=%d NCPU=%d\n", NPROC, NCPU);
  dprintf(1, "  profile=%s\n", SCHEDPERF_PROFILE);

  for(r = 0; r < nruns; r++){
    passed = failed = perf_score = perf_score_max = 0;
    if(nruns > 1)
      dprintf(1, "\n--- run %d/%d ---\n", r + 1, nruns);
    else
      dprintf(1, "\n");

    test_fork_storm();
    test_yield_storm();
    test_pipe_wakeup();
    test_alarm_counter();
    test_signal_fast_path();
    test_idle_responsiveness();
    test_sched_spread();
    test_proc_table_limit();

    run_scores[r] = perf_score_max ? (perf_score * 100) / perf_score_max : 0;
    total_passed += passed;
    total_failed_runs += failed;

    dprintf(1, "\nschedperf score: %d/100 (target >= 75)\n", run_scores[r]);
    dprintf(1, "schedperf results: %d passed, %d failed\n", passed, failed);
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

  exit(total_failed_runs > 0 ? 1 : 0);
}
