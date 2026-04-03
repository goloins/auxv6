// fsperf.c - filesystem, inode-cache, and buffer-cache stress tests
//
// Exercises the fs performance changes from kernel-perf-hardening:
//   - Inode cache hash table (ICACHE_HASH_SIZE=64, O(1) lookup via ihash)
//   - Buffer cache hash table (BCACHE_HASH_SIZE=64, O(1) lookup via bhash)
//   - Raised NINODE (200), NFILE (256), NOFILE (32), NBUF (128)
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
// T1: fd-ceiling -- confirm we can open up to NOFILE-2 descriptors per
//     process (NOFILE raised from 16 to 32; regression if we hit 16).
// ---------------------------------------------------------------------------
#define FD_TARGET (NOFILE - 2)   // leave room for stdin/stdout/stderr

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
// T6: inode-limit -- exercise up to ~NINODE distinct inodes being resident
//     simultaneously (raised from 50 to 200).
//     We open IOPEN_COUNT files at the same time within a single process.
// ---------------------------------------------------------------------------
#define IOPEN_COUNT  80   // well above old NINODE=50

static void
test_inode_limit(void)
{
  char path[64];
  int i, err = 0;
  int fds[IOPEN_COUNT];
  uint t0 = uptime();

  // Create files
  for(i = 0; i < IOPEN_COUNT; i++){
    mkpath(path, sizeof(path), 2000 + i);
    int fd = open(path, O_WRONLY | O_CREATE);
    if(fd < 0){ err++; continue; }
    write(fd, "x", 1);
    close(fd);
  }

  // Open all simultaneously
  int opened = 0;
  for(i = 0; i < IOPEN_COUNT; i++){
    mkpath(path, sizeof(path), 2000 + i);
    fds[i] = open(path, O_RDONLY);
    if(fds[i] < 0) err++;
    else opened++;
  }

  // Close all
  for(i = 0; i < opened; i++)
    if(fds[i] >= 0) close(fds[i]);

  // Unlink
  for(i = 0; i < IOPEN_COUNT; i++){
    mkpath(path, sizeof(path), 2000 + i);
    unlink(path);
  }

  if(err == 0)
    PASS("inode-limit");
  else {
    dprintf(1, "[FAIL] inode-limit: %d errors (opened %d/%d)\n",
            err, opened, IOPEN_COUNT);
    failed++;
  }

  perf_record("inode-limit", "open-fds", opened, IOPEN_COUNT, 10);
  perf_record("inode-limit-rate", "open/s", ops_per_sec(opened, t0, uptime()), 600, 6);
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
  const char *path = "/tmp/fsperf_hashcheck";
  int i, err = 0;
  uint t0 = uptime();

  // Write a known pattern
  int fd = open(path, O_WRONLY | O_CREATE);
  if(fd < 0){ FAIL("hash-correctness", "create failed"); return; }
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
// main
// ---------------------------------------------------------------------------
int
main(int argc, char *argv[])
{
  (void)argc; (void)argv;

  dprintf(1, "fsperf: inode-cache and buffer-cache stress\n");
  dprintf(1, "  NINODE=%d NFILE=%d NOFILE=%d NBUF=%d\n",
          NINODE, NFILE, NOFILE, NBUF);
  dprintf(1, "\n");

  // Ensure /tmp exists (tmpfs or similar)
  mkdir("/tmp");

  test_fd_ceiling();
  test_inode_churn();
  test_bcache_sequential();
  test_concurrent_openers();
  test_parallel_writers();
  test_inode_limit();
  test_hash_correctness();

  dprintf(1, "\nfsperf score: %d/100 (target >= 75)\n",
          perf_score_max ? (perf_score * 100) / perf_score_max : 0);
  dprintf(1, "\nfsperf results: %d passed, %d failed\n", passed, failed);
  exit(failed > 0 ? 1 : 0);
}
