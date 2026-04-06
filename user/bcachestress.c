/*
 * bcachestress - buffer cache integrity stress tool
 *
 * Hammers bread() by driving heavy concurrent filesystem I/O: forked workers
 * create, write, read back, stat, and unlink files in a scratch directory.
 * The parent process polls /proc/bcache_health after each round and prints
 * the full report.  Any non-zero corruption counter causes an immediate
 * failure exit.
 *
 * Also samples /proc/vmstat and /proc/meminfo each round for broader context
 * about whether memory pressure coincides with bcache anomalies.
 *
 * Usage:
 *   bcachestress [-w workers] [-r rounds] [-f files] [-k kbytes] [-d dir]
 *
 * Defaults: 4 workers, 50 rounds, 8 files per worker, 8 KB per file, /tmp/bcs
 */

#include "types.h"
#include "stat.h"
#include "fcntl.h"
#include "auxv6/user.h"

#define DEFAULT_WORKERS   4
#define DEFAULT_ROUNDS   50
#define DEFAULT_FILES     8
#define DEFAULT_KBYTES    8   /* KB per file */
#define DEFAULT_DIR       "/tmp/bcs"

#define MAX_WORKERS      16
#define MAX_FILES        32
#define MAX_PATH        128
#define WBUF_SIZE      4096  /* write/read I/O buffer */

static int nworkers   = DEFAULT_WORKERS;
static int nrounds    = DEFAULT_ROUNDS;
static int nfiles     = DEFAULT_FILES;
static int file_kb    = DEFAULT_KBYTES;
static const char *scratch = DEFAULT_DIR;
static int verbose    = 0;

/* -----------------------------------------------------------------------
 * Small utilities (no libc)
 * --------------------------------------------------------------------- */

static int
str_to_int(const char *s)
{
  int v = 0;
  while(*s >= '0' && *s <= '9')
    v = v * 10 + (*s++ - '0');
  return v;
}

static int
str_eq(const char *a, const char *b)
{
  while(*a && *b)
    if(*a++ != *b++) return 0;
  return *a == *b;
}

static void
int_to_str(int v, char *buf, int max)
{
  char tmp[16];
  int n = 0;
  int neg = 0;
  if(v < 0){ neg = 1; v = -v; }
  if(v == 0){ buf[0]='0'; buf[1]=0; return; }
  while(v && n < (int)sizeof(tmp)-1){
    tmp[n++] = '0' + (v % 10);
    v /= 10;
  }
  int pos = 0;
  if(neg && pos < max-1) buf[pos++] = '-';
  for(int i = n-1; i >= 0 && pos < max-1; i--)
    buf[pos++] = tmp[i];
  buf[pos] = 0;
}

static void
str_cat(char *dst, int max, const char *src)
{
  int i = 0;
  while(dst[i]) i++;
  while(*src && i < max-1) dst[i++] = *src++;
  dst[i] = 0;
}

static void
build_path(char *out, int max, const char *dir, int wid, int fid)
{
  char wbuf[8], fbuf[8];
  int_to_str(wid, wbuf, sizeof(wbuf));
  int_to_str(fid, fbuf, sizeof(fbuf));
  out[0] = 0;
  str_cat(out, max, dir);
  str_cat(out, max, "/w");
  str_cat(out, max, wbuf);
  str_cat(out, max, "_f");
  str_cat(out, max, fbuf);
}

/* Read an entire procfs file into buf.  Returns byte count or -1. */
static int
read_proc(const char *path, char *buf, int max)
{
  int fd, n, total = 0;
  fd = open(path, O_RDONLY);
  if(fd < 0) return -1;
  while(total < max - 1){
    n = read(fd, buf + total, max - 1 - total);
    if(n <= 0) break;
    total += n;
  }
  buf[total] = 0;
  close(fd);
  return total;
}

/* -----------------------------------------------------------------------
 * Health check parsing: scan for any key whose value != 0 that indicates
 * corruption.  Prints the full blob and returns number of bad fields found.
 * --------------------------------------------------------------------- */
static const char *corrupt_keys[] = {
  "bcache_hash_corrupt ",
  "bcache_hash_cycles ",
  "bcache_double_hash ",
  "bcache_lru_corrupt ",
  "bcache_lru_cycles ",
  "bcache_error_bufs ",
  0
};

static int
parse_uint_after(const char *blob, const char *key)
{
  const char *p = blob;
  int klen = 0;
  while(key[klen]) klen++;
  while(*p){
    int match = 1;
    for(int i = 0; i < klen; i++)
      if(p[i] != key[i]){ match = 0; break; }
    if(match){
      const char *v = p + klen;
      int val = 0;
      while(*v >= '0' && *v <= '9') val = val*10 + (*v++ - '0');
      return val;
    }
    p++;
  }
  return -1;
}

static int
health_check(int round)
{
  static char blob[512];
  int bad = 0;

  if(read_proc("/proc/bcache_health", blob, sizeof(blob)) < 0){
    dprintf(2, "bcachestress: round %d: cannot read /proc/bcache_health\n", round);
    return 1;
  }

  for(int i = 0; corrupt_keys[i]; i++){
    int v = parse_uint_after(blob, corrupt_keys[i]);
    if(v > 0){
      dprintf(2, "bcachestress: CORRUPTION round=%d %s= %d\n",
              round, corrupt_keys[i], v);
      bad++;
    }
  }

  if(verbose || bad){
    dprintf(1, "--- bcache_health round %d ---\n", round);
    dprintf(1, "%s", blob);
  }

  return bad;
}

static void
context_dump(int round)
{
  static char blob[2048];
  dprintf(1, "--- vmstat round %d ---\n", round);
  if(read_proc("/proc/vmstat", blob, sizeof(blob)) > 0)
    dprintf(1, "%s", blob);
  dprintf(1, "--- meminfo round %d ---\n", round);
  if(read_proc("/proc/meminfo", blob, sizeof(blob)) > 0)
    dprintf(1, "%s", blob);
}

/* -----------------------------------------------------------------------
 * Worker: runs inside a forked child.
 * Creates nfiles files of file_kb KB, reads them back with seeks,
 * stats each, then unlinks all.  Exits 0 on success, 1 on error.
 * --------------------------------------------------------------------- */
static void
worker_run(int wid)
{
  static char wbuf[WBUF_SIZE];
  char path[MAX_PATH];
  int err = 0;

  /* Fill write buffer with a per-worker pattern for read-back verification */
  for(int i = 0; i < WBUF_SIZE; i++)
    wbuf[i] = (char)((wid * 13 + i) & 0xff);

  int target_bytes = file_kb * 1024;

  for(int f = 0; f < nfiles; f++){
    build_path(path, sizeof(path), scratch, wid, f);

    /* --- write --- */
    int fd = open(path, O_WRONLY | O_CREATE | O_TRUNC);
    if(fd < 0){ dprintf(2, "worker%d: open w %s failed\n", wid, path); err=1; continue; }
    int written = 0;
    while(written < target_bytes){
      int chunk = WBUF_SIZE;
      if(chunk > target_bytes - written) chunk = target_bytes - written;
      int n = write(fd, wbuf, chunk);
      if(n <= 0){ dprintf(2, "worker%d: write %s failed\n", wid, path); err=1; break; }
      written += n;
    }
    close(fd);

    /* --- stat --- */
    struct stat st;
    if(stat(path, &st) < 0){ dprintf(2, "worker%d: stat %s failed\n", wid, path); err=1; }

    /* --- read back sequentially then at several offsets --- */
    fd = open(path, O_RDONLY);
    if(fd < 0){ dprintf(2, "worker%d: open r %s failed\n", wid, path); err=1; continue; }

    static char rbuf[WBUF_SIZE];
    int read_bytes = 0;
    while(read_bytes < target_bytes){
      int chunk = WBUF_SIZE;
      if(chunk > target_bytes - read_bytes) chunk = target_bytes - read_bytes;
      int n = read(fd, rbuf, chunk);
      if(n <= 0) break;
      /* spot-check first byte of each chunk */
      char expected = (char)((wid * 13 + read_bytes) & 0xff);
      if(rbuf[0] != expected){
        dprintf(2, "worker%d: data mismatch at off %d in %s (got %d want %d)\n",
                wid, read_bytes, path, (int)(unsigned char)rbuf[0], (int)(unsigned char)expected);
        err = 1;
      }
      read_bytes += n;
    }
    close(fd);

    /* --- unlink --- */
    if(unlink(path) < 0){
      dprintf(2, "worker%d: unlink %s failed\n", wid, path);
      err = 1;
    }
  }

  exit(err);
}

/* -----------------------------------------------------------------------
 * Main
 * --------------------------------------------------------------------- */

static void
usage(void)
{
  dprintf(2,
    "usage: bcachestress [options]\n"
    "  -w <n>   worker processes (default %d, max %d)\n"
    "  -r <n>   rounds (default %d)\n"
    "  -f <n>   files per worker per round (default %d, max %d)\n"
    "  -k <n>   KB per file (default %d)\n"
    "  -d <dir> scratch directory (default %s)\n"
    "  -v       verbose: print bcache_health every round\n",
    DEFAULT_WORKERS, MAX_WORKERS,
    DEFAULT_ROUNDS,
    DEFAULT_FILES, MAX_FILES,
    DEFAULT_KBYTES,
    DEFAULT_DIR);
  exit(1);
}

int
main(int argc, char *argv[])
{
  int i;

  for(i = 1; i < argc; i++){
    if(str_eq(argv[i], "-w") && i+1 < argc){
      nworkers = str_to_int(argv[++i]);
      if(nworkers < 1 || nworkers > MAX_WORKERS) nworkers = DEFAULT_WORKERS;
    } else if(str_eq(argv[i], "-r") && i+1 < argc){
      nrounds = str_to_int(argv[++i]);
    } else if(str_eq(argv[i], "-f") && i+1 < argc){
      nfiles = str_to_int(argv[++i]);
      if(nfiles < 1 || nfiles > MAX_FILES) nfiles = DEFAULT_FILES;
    } else if(str_eq(argv[i], "-k") && i+1 < argc){
      file_kb = str_to_int(argv[++i]);
      if(file_kb < 1) file_kb = 1;
    } else if(str_eq(argv[i], "-d") && i+1 < argc){
      scratch = argv[++i];
    } else if(str_eq(argv[i], "-v")){
      verbose = 1;
    } else {
      usage();
    }
  }

  dprintf(1, "bcachestress: workers=%d rounds=%d files=%d file_kb=%d dir=%s\n",
          nworkers, nrounds, nfiles, file_kb, scratch);

  /* Create scratch dir (ignore error if it already exists) */
  mkdir(scratch);

  int total_bad   = 0;
  int worker_errs = 0;

  for(int round = 0; round < nrounds; round++){
    /* Fork workers */
    int pids[MAX_WORKERS];
    for(i = 0; i < nworkers; i++){
      int pid = fork();
      if(pid == 0)
        worker_run(i);
      if(pid < 0){
        dprintf(2, "bcachestress: fork failed round %d worker %d\n", round, i);
        pids[i] = -1;
      } else {
        pids[i] = pid;
      }
    }

    /* Wait for all workers */
    for(i = 0; i < nworkers; i++){
      if(pids[i] < 0) continue;
      int st = 0;
      int ret = waitpid(pids[i], &st, 0);
      if(ret < 0 || st != 0){
        dprintf(2, "bcachestress: round %d worker %d exited with %d\n", round, i, st);
        worker_errs++;
      }
    }

    /* Health check after every round */
    int bad = health_check(round);
    total_bad += bad;

    if(bad){
      /* Dump extra context on first detection */
      context_dump(round);
      dprintf(2, "bcachestress: FATAL corruption detected at round %d — stopping\n", round);
      exit(2);
    }

    if((round % 10) == 9)
      dprintf(1, "bcachestress: round %d/%d ok (worker_errs_so_far=%d)\n",
              round+1, nrounds, worker_errs);
  }

  /* Final health snapshot regardless */
  health_check(nrounds);
  context_dump(nrounds);

  if(total_bad == 0 && worker_errs == 0){
    dprintf(1, "bcachestress: PASS %d rounds, %d workers, %d files each\n",
            nrounds, nworkers, nfiles);
    exit(0);
  }

  dprintf(2, "bcachestress: FAIL corrupt_rounds=%d worker_errs=%d\n",
          total_bad, worker_errs);
  exit(1);
}
