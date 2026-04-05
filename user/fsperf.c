// fsperf.c - filesystem, inode-cache, and buffer-cache stress tests
//
// Exercises the fs performance changes from kernel-perf-hardening:
//   - Inode cache hash table (ICACHE_HASH_SIZE=64, O(1) lookup via ihash)
//   - Buffer cache hash table (BCACHE_HASH_SIZE=64, O(1) lookup via bhash)
//   - Raised cache/descriptor budgets (NINODE/NOFILE*/NBUF)
//
// Usage: fsperf
// Prints [PASS]/[FAIL] for each sub-test and a final summary.
// Note: creates/removes files in /tmp/fsperf_* (must be writable tmpfs).

#include "types.h"
#include "stat.h"
#include "auxv6/user.h"
#include "fcntl.h"
#include "param.h"

#define PASS(name) do { dprintf(1, "[PASS] %s\n", name); passed++; } while(0)
#define FAIL(name, why) do { dprintf(1, "[FAIL] %s: %s\n", name, why); failed++; } while(0)

static int passed = 0;
static int failed = 0;
static int perf_score = 0;
static int perf_score_max = 0;

#define FSPERF_PROFILE "2026-04-03-r2"

#define MOUNTSTATS_BUF 8192

static int
path_is_mount_prefix_len(const char *mountp, int mlen, const char *target)
{
  int i;

  if(mountp == 0 || target == 0 || mlen <= 0)
    return 0;

  if(mlen == 1 && mountp[0] == '/')
    return target[0] == '/';

  for(i = 0; i < mlen; i++){
    if(target[i] != mountp[i])
      return 0;
  }

  return target[mlen] == '\0' || target[mlen] == '/';
}

static int
copy_token(char *dst, int dsz, const char *src, int len)
{
  int i;

  if(dsz <= 0)
    return -1;
  if(len >= dsz)
    len = dsz - 1;
  for(i = 0; i < len; i++)
    dst[i] = src[i];
  dst[len] = '\0';
  return len;
}

/* Best-effort parse of /proc/mountstats. Returns 0 on success. */
static int
detect_tmp_backend(char *fstype, int fstype_sz)
{
  char buf[MOUNTSTATS_BUF + 1];
  int fd;
  int n;
  int i;
  int best_len;

  if(fstype == 0 || fstype_sz <= 0)
    return -1;
  fstype[0] = '\0';

  fd = open("/proc/mountstats", O_RDONLY);
  if(fd < 0)
    return -1;
  n = read(fd, buf, MOUNTSTATS_BUF);
  close(fd);
  if(n <= 0)
    return -1;
  if(n > MOUNTSTATS_BUF)
    n = MOUNTSTATS_BUF;
  buf[n] = '\0';

  i = 0;
  best_len = -1;
  while(i < n){
    int ls = i;
    int le;
    int tok = 0;
    int j;
    int in_tok;
    int p_s = -1, p_e = -1;
    int t_s = -1, t_e = -1;

    while(i < n && buf[i] != '\n')
      i++;
    le = i;
    if(i < n && buf[i] == '\n')
      i++;

    in_tok = 0;
    for(j = ls; j < le; j++){
      char c = buf[j];
      if(c == ' ' || c == '\t'){
        in_tok = 0;
        continue;
      }
      if(!in_tok){
        tok++;
        in_tok = 1;
        if(tok == 2)
          p_s = j;
        else if(tok == 3)
          t_s = j;
      }
      if(tok == 2)
        p_e = j + 1;
      else if(tok == 3)
        t_e = j + 1;
    }

    if(p_s >= 0 && p_e > p_s && t_s >= 0 && t_e > t_s){
      int plen = p_e - p_s;
      if(path_is_mount_prefix_len(&buf[p_s], plen, "/tmp") && plen > best_len){
        best_len = plen;
        copy_token(fstype, fstype_sz, &buf[t_s], t_e - t_s);
      }
    }
  }

  return best_len >= 0 ? 0 : -1;
}

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
// Helpers
// ---------------------------------------------------------------------------

static void
mkpath(char *buf, int bufsz, int idx)
{
  int i, n;
  const char *prefix = "/tmp/fsperf_";
  n = 0;
  for(i = 0; prefix[i] && n < bufsz-1; i++)
    buf[n++] = prefix[i];
  // append decimal idx
  char tmp[12];
  int tlen = 0;
  if(idx == 0){
    tmp[tlen++] = '0';
  } else {
    int v = idx;
    while(v > 0 && tlen < 11){
      tmp[tlen++] = '0' + (v % 10);
      v /= 10;
    }
    // reverse
    int a = 0, b = tlen - 1;
    while(a < b){
      char c = tmp[a]; tmp[a] = tmp[b]; tmp[b] = c;
      a++; b--;
    }
  }
  for(i = 0; i < tlen && n < bufsz-1; i++)
    buf[n++] = tmp[i];
  buf[n] = '\0';
}

// Create a file, write `sz` bytes of pattern, close it.
static int
create_fill(const char *path, int sz)
{
  int fd = open(path, O_WRONLY | O_CREATE);
  if(fd < 0) return -1;
  char buf[64];
  int i;
  for(i = 0; i < (int)sizeof(buf); i++)
    buf[i] = (char)(i & 0x7f);
  int written = 0;
  while(written < sz){
    int n = sz - written;
    if(n > (int)sizeof(buf)) n = (int)sizeof(buf);
    if(write(fd, buf, n) != n){ close(fd); return -1; }
    written += n;
  }
  close(fd);
  return 0;
}

static void
cleanup(int nfiles)
{
  char path[64];
  int i;
  for(i = 0; i < nfiles; i++){
    mkpath(path, sizeof(path), i);
    unlink(path);
  }
}

// ---------------------------------------------------------------------------
// T1: fd-ceiling -- confirm we can open up to NOFILE-3 additional descriptors
//     per process (stdin/stdout/stderr already consume three slots).
// ---------------------------------------------------------------------------
#define FD_TARGET (NOFILE - 3)

static void
test_fd_ceiling(void)
{
  uint t0 = uptime();
  // Create a scratch file
  int scratchfd = open("/tmp/fsperf_scratch", O_WRONLY | O_CREATE);
  if(scratchfd < 0){
    FAIL("fd-ceiling", "could not create scratch file");
    return;
  }
  write(scratchfd, "x", 1);
  close(scratchfd);

  int fds[FD_TARGET];
  int i, opened = 0, err = 0;

  for(i = 0; i < FD_TARGET; i++){
    fds[i] = open("/tmp/fsperf_scratch", O_RDONLY);
    if(fds[i] < 0){
      err = 1;
      break;
    }
    opened++;
  }

  // Close all we opened
  for(i = 0; i < opened; i++)
    close(fds[i]);
  unlink("/tmp/fsperf_scratch");

  if(!err && opened == FD_TARGET)
    PASS("fd-ceiling");
  else {
    dprintf(1, "[FAIL] fd-ceiling: opened %d/%d\n", opened, FD_TARGET);
    failed++;
  }

  perf_record("fd-ceiling", "fds", opened, FD_TARGET, 12);
  perf_record("fd-open-rate", "open/s", ops_per_sec(opened, t0, uptime()), 700, 8);
}

// ---------------------------------------------------------------------------
// T2: inode-churn -- create NFILES distinct files (drives icache eviction
//     and insertion), open each, stat each, then close and unlink all.
//     Tests O(1) iget() hash path on both hits and cold misses.
// ---------------------------------------------------------------------------
#define INODE_NFILES 60

static void
test_inode_churn(void)
{
  char path[64];
  int i, err = 0;
  uint t0 = uptime();

  // Phase 1: create
  for(i = 0; i < INODE_NFILES; i++){
    mkpath(path, sizeof(path), i);
    int fd = open(path, O_WRONLY | O_CREATE);
    if(fd < 0){ err++; continue; }
    write(fd, path, 4);
    close(fd);
  }

  // Phase 2: open + stat each
  for(i = 0; i < INODE_NFILES; i++){
    mkpath(path, sizeof(path), i);
    struct stat st;
    int fd = open(path, O_RDONLY);
    if(fd < 0){ err++; continue; }
    if(fstat(fd, &st) < 0) err++;
    close(fd);
  }

  // Phase 3: unlink all
  cleanup(INODE_NFILES);

  if(err == 0)
    PASS("inode-churn");
  else {
    dprintf(1, "[FAIL] inode-churn: %d errors\n", err);
    failed++;
  }

  perf_record("inode-churn", "file-op/s",
              ops_per_sec(INODE_NFILES * 3, t0, uptime()),
              1700,
              16);
}

// ---------------------------------------------------------------------------
// T3: bcache-sequential -- read a large file in 512-byte chunks to drive
//     many distinct block numbers through the buffer cache hash.
//     With NBUF=128 and BCACHE_HASH_SIZE=64, we sweep past a full hash
//     table fill to confirm eviction and re-insertion work correctly.
// ---------------------------------------------------------------------------
#define BSEQ_BLKSZ   512
#define BSEQ_NBLKS   160   // > NBUF=128 to force eviction

static void
test_bcache_sequential(void)
{
  const char *path = "/tmp/fsperf_bigfile";
  int totalsize = BSEQ_BLKSZ * BSEQ_NBLKS;
  int fd, i, err = 0;
  uint t0 = uptime();

  // Create
  fd = open(path, O_WRONLY | O_CREATE);
  if(fd < 0){ FAIL("bcache-sequential", "open for write failed"); return; }
  char blk[BSEQ_BLKSZ];
  for(i = 0; i < BSEQ_BLKSZ; i++) blk[i] = (char)(i & 0xff);
  for(i = 0; i < BSEQ_NBLKS; i++){
    if(write(fd, blk, BSEQ_BLKSZ) != BSEQ_BLKSZ) err++;
  }
  close(fd);

  if(err){ unlink(path); FAIL("bcache-sequential", "write failed"); return; }

  // Read pass 1 (cold)
  fd = open(path, O_RDONLY);
  if(fd < 0){ unlink(path); FAIL("bcache-sequential", "open for read failed"); return; }
  int total = 0;
  while(1){
    int n = read(fd, blk, BSEQ_BLKSZ);
    if(n <= 0) break;
    total += n;
  }
  close(fd);
  if(total != totalsize){ unlink(path); FAIL("bcache-sequential", "short read pass1"); return; }

  // Read pass 2 (warm -- hash hits)
  fd = open(path, O_RDONLY);
  if(fd < 0){ unlink(path); FAIL("bcache-sequential", "re-open failed"); return; }
  total = 0;
  while(1){
    int n = read(fd, blk, BSEQ_BLKSZ);
    if(n <= 0) break;
    total += n;
  }
  close(fd);
  unlink(path);

  if(total == totalsize)
    PASS("bcache-sequential");
  else
    FAIL("bcache-sequential", "short read pass2");

  perf_record("bcache-sequential", "KB/s",
              ops_per_sec((totalsize * 2) / 1024, t0, uptime()),
              2600,
              20);
}

// ---------------------------------------------------------------------------
// T4: concurrent-openers -- NOPENERS children all open, read, and close the
//     same file concurrently.  Stresses icache.lock contention + inode ref
//     counting with the hash table active.
// ---------------------------------------------------------------------------
#define NOPENERS      12
#define OPENER_READS  20

static void
test_concurrent_openers(void)
{
  const char *path = "/tmp/fsperf_shared";
  int i;
  uint t0 = uptime();

  // Create shared file
  if(create_fill(path, 512) < 0){
    FAIL("concurrent-openers", "could not create shared file");
    return;
  }

  for(i = 0; i < NOPENERS; i++){
    if(fork() == 0){
      int j, fd;
      char buf[32];
      for(j = 0; j < OPENER_READS; j++){
        fd = open(path, O_RDONLY);
        if(fd < 0) exit(1);
        read(fd, buf, sizeof(buf));
        close(fd);
      }
      exit(0);
    }
  }

  int bad = 0;
  for(i = 0; i < NOPENERS; i++){
    if(wait() < 0)
      bad++;
  }

  unlink(path);

  if(bad == 0)
    PASS("concurrent-openers");
  else
    FAIL("concurrent-openers", "child returned error");

  perf_record("concurrent-openers", "open/s",
              ops_per_sec(NOPENERS * OPENER_READS, t0, uptime()),
              1800,
              14);
}

// ---------------------------------------------------------------------------
// T5: parallel-writers -- NWRITERS children each create, write, and unlink
//     their own private file concurrently.  Stresses both icache and bcache
//     under concurrent modification.
// ---------------------------------------------------------------------------
#define NWRITERS        8
#define WRITER_SZ    4096

static void
test_parallel_writers(void)
{
  int i;
  uint t0 = uptime();

  for(i = 0; i < NWRITERS; i++){
    if(fork() == 0){
      char path[64];
      mkpath(path, sizeof(path), 1000 + i);   // distinct from inode-churn range
      if(create_fill(path, WRITER_SZ) < 0)
        exit(1);
      // verify by re-reading
      int fd = open(path, O_RDONLY);
      if(fd < 0){ unlink(path); exit(1); }
      char buf[64];
      int total = 0;
      int n;
      while((n = read(fd, buf, sizeof(buf))) > 0)
        total += n;
      close(fd);
      unlink(path);
      exit(total == WRITER_SZ ? 0 : 1);
    }
  }

  int bad = 0;
  for(i = 0; i < NWRITERS; i++){
    if(wait() < 0)
      bad++;
  }

  if(bad == 0)
    PASS("parallel-writers");
  else
    FAIL("parallel-writers", "writer child error");

  perf_record("parallel-writers", "KB/s",
              ops_per_sec((NWRITERS * WRITER_SZ) / 1024, t0, uptime()),
              900,
              14);
}

// ---------------------------------------------------------------------------
// T6: inode-limit -- exercise many resident inodes concurrently without
//     tripping per-process NOFILE limits by spreading opens across children.
// ---------------------------------------------------------------------------
#define IOPEN_COUNT  64   // above old NINODE=50 and below NPROC headroom

static void
test_inode_limit(void)
{
  char path[64];
  int i, err = 0;
  int ack[2];
  int rel[2];
  int pids[IOPEN_COUNT];
  int success = 0;
  uint t0 = uptime();

  // Create files
  for(i = 0; i < IOPEN_COUNT; i++){
    mkpath(path, sizeof(path), 2000 + i);
    int fd = open(path, O_WRONLY | O_CREATE);
    if(fd < 0){ err++; continue; }
    write(fd, "x", 1);
    close(fd);
  }

  if(pipe(ack) < 0 || pipe(rel) < 0){
    for(i = 0; i < IOPEN_COUNT; i++){
      mkpath(path, sizeof(path), 2000 + i);
      unlink(path);
    }
    FAIL("inode-limit", "pipe setup failed");
    return;
  }

  // Open one file per child, then hold it open until parent releases.
  for(i = 0; i < IOPEN_COUNT; i++){
    int pid = fork();
    if(pid < 0){
      err++;
      break;
    }
    if(pid == 0){
      char c = '0';
      int fd;
      close(ack[0]);
      close(rel[1]);
      mkpath(path, sizeof(path), 2000 + i);
      fd = open(path, O_RDONLY);
      if(fd >= 0)
        c = '1';
      write(ack[1], &c, 1);
      if(fd >= 0){
        char relc;
        read(rel[0], &relc, 1);
        close(fd);
        exit(0);
      }
      exit(1);
    }
    pids[i] = pid;
  }
  close(ack[1]);
  close(rel[0]);

  // Count child open successes.
  for(i = 0; i < IOPEN_COUNT; i++){
    char c;
    if(read(ack[0], &c, 1) != 1){
      err++;
      break;
    }
    if(c == '1')
      success++;
  }
  close(ack[0]);

  // Release children that successfully opened files.
  for(i = 0; i < success; i++){
    char relc = 'x';
    write(rel[1], &relc, 1);
  }
  close(rel[1]);

  // Reap children.
  for(i = 0; i < IOPEN_COUNT; i++){
    if(waitpid(pids[i], 0, 0) < 0)
      err++;
  }

  // Unlink
  for(i = 0; i < IOPEN_COUNT; i++){
    mkpath(path, sizeof(path), 2000 + i);
    unlink(path);
  }

  if(err == 0 && success == IOPEN_COUNT)
    PASS("inode-limit");
  else {
    dprintf(1, "[FAIL] inode-limit: %d errors (opened %d/%d)\n",
            err, success, IOPEN_COUNT);
    failed++;
  }

  perf_record("inode-limit", "open-fds", success, IOPEN_COUNT, 10);
  perf_record("inode-limit-rate", "open/s", ops_per_sec(success, t0, uptime()), 600, 6);
}

// ---------------------------------------------------------------------------
// T7: hash-correctness -- write a file, close it, reopen it, and verify the
//     data is intact.  If the hash-table insertion/eviction logic in bio.c
//     is wrong (e.g., stale pointer in hash chain) this tends to surface as
//     corrupt reads.
// ---------------------------------------------------------------------------
#define HASH_CHECK_SZ  1024
#define HASH_CHECK_REPS 40

static void
test_hash_correctness(void)
{
  char path[64];
  int i, err = 0;
  uint t0 = uptime();
  int tries;

  mkpath(path, sizeof(path), 4096);

  // Basic writable-/tmp preflight.
  {
    int tfd = open("/tmp", O_RDONLY);
    if(tfd < 0){
      FAIL("hash-correctness", "cannot access /tmp");
      return;
    }
    close(tfd);
  }

  // Write a known pattern
  int fd = -1;
  for(tries = 0; tries < 5; tries++){
    unlink(path);
    fd = open(path, O_WRONLY | O_CREATE);
    if(fd >= 0)
      break;
    sleep(0);   // yield and retry in case of transient global file pressure
  }
  if(fd < 0){
    dprintf(1, "[FAIL] hash-correctness: create failed after %d retries\n", tries);
    failed++;
    return;
  }
  char wbuf[HASH_CHECK_SZ];
  for(i = 0; i < HASH_CHECK_SZ; i++) wbuf[i] = (char)((i * 7 + 3) & 0xff);
  if(write(fd, wbuf, HASH_CHECK_SZ) != HASH_CHECK_SZ)
    err++;
  close(fd);

  // Re-open and verify HASH_CHECK_REPS times to stress hash lookups
  for(i = 0; i < HASH_CHECK_REPS && !err; i++){
    fd = open(path, O_RDONLY);
    if(fd < 0){ err++; break; }
    char rbuf[HASH_CHECK_SZ];
    int n = read(fd, rbuf, HASH_CHECK_SZ);
    close(fd);
    if(n != HASH_CHECK_SZ){ err++; break; }
    int j;
    for(j = 0; j < HASH_CHECK_SZ; j++){
      if(rbuf[j] != wbuf[j]){ err++; break; }
    }
  }

  unlink(path);

  if(err == 0)
    PASS("hash-correctness");
  else
    FAIL("hash-correctness", "data mismatch or read error");

  perf_record("hash-correctness", "verify/s",
              ops_per_sec(HASH_CHECK_REPS, t0, uptime()),
              1100,
              10);
}

// ---------------------------------------------------------------------------
// T8: poststress-create -- after all heavy churn, repeatedly create/open/
//     close/unlink files to catch lingering resource-leak edge cases.
// ---------------------------------------------------------------------------
#define POSTSTRESS_TRIES 80

static void
test_poststress_create(void)
{
  int i;
  int ok = 1;
  uint t0 = uptime();
  char path[64];

  for(i = 0; i < POSTSTRESS_TRIES; i++){
    int fd;
    mkpath(path, sizeof(path), 5000 + i);
    unlink(path);
    fd = open(path, O_WRONLY | O_CREATE);
    if(fd < 0){
      ok = 0;
      break;
    }
    if(write(fd, "ok", 2) != 2){
      close(fd);
      ok = 0;
      break;
    }
    close(fd);

    fd = open(path, O_RDONLY);
    if(fd < 0){
      ok = 0;
      break;
    }
    close(fd);
    unlink(path);
  }

  if(ok)
    PASS("poststress-create");
  else
    FAIL("poststress-create", "create/open/unlink failed after stress");

  perf_record("poststress-create", "iter/s",
              ops_per_sec(ok ? POSTSTRESS_TRIES : i, t0, uptime()),
              250,
              8);
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
      dprintf(2, "usage: fsperf [-n runs]\n");
      exit(1);
    }
  } else if(argc != 1){
    dprintf(2, "usage: fsperf [-n runs]\n");
    exit(1);
  }

  int run_scores[MAX_RUNS];
  int total_passed = 0, total_failed_runs = 0;
  char tmpfs_type[32];

  dprintf(1, "fsperf: inode-cache and buffer-cache stress\n");
    dprintf(1, "  NINODE=%d NOFILE_DEFAULT=%d NOFILE_HARD=%d NBUF=%d\n",
      NINODE, NOFILE_DEFAULT, NOFILE_HARD, NBUF);
  dprintf(1, "  profile=%s\n", FSPERF_PROFILE);

  if(detect_tmp_backend(tmpfs_type, sizeof(tmpfs_type)) == 0){
    dprintf(1, "  /tmp backend=%s\n", tmpfs_type);
    if(strcmp(tmpfs_type, "tmpfs") != 0)
      dprintf(1, "  note: profile targets assume /tmp on tmpfs; backend mismatch may reduce throughput\n");
  } else {
    dprintf(1, "  /tmp backend=unknown (could not parse /proc/mountstats)\n");
  }

  // Ensure /tmp exists (tmpfs or similar)
  mkdir("/tmp");

  for(r = 0; r < nruns; r++){
    passed = failed = perf_score = perf_score_max = 0;
    if(nruns > 1)
      dprintf(1, "\n--- run %d/%d ---\n", r + 1, nruns);
    else
      dprintf(1, "\n");

    test_fd_ceiling();
    test_inode_churn();
    test_bcache_sequential();
    test_concurrent_openers();
    test_parallel_writers();
    test_inode_limit();
    test_hash_correctness();
    test_poststress_create();

    run_scores[r] = perf_score_max ? (perf_score * 100) / perf_score_max : 0;
    total_passed += passed;
    total_failed_runs += failed;

    dprintf(1, "\nfsperf score: %d/100 (target >= 75)\n", run_scores[r]);
    dprintf(1, "fsperf results: %d passed, %d failed\n", passed, failed);
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
