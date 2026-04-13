/*
 * stackgrowtest - on-demand user stack growth regression test
 *
 * Tests that the kernel correctly grows the user stack on demand when the
 * stack pointer descends into the guard page, up to USER_STACK_MAX_PAGES.
 *
 * Test 1 - deep recursion: recurse deep enough to exhaust the initial
 *   USER_STACK_PAGES (4 pages = 16 KiB) fixed budget and cause growth.
 *   Each frame is padded to ~512 bytes so the depth required to cross the
 *   16 KiB boundary is predictable (~50 frames).  We recurse to 400.
 *
 * Test 2 - fork inheritance: verify that a child of a grown-stack process
 *   can itself recurse without issue (stack bounds are inherited via fork).
 *
 * Test 3 - SIGSEGV on max exceeded: install a SIGSEGV handler and verify
 *   that attempting to consume more than USER_STACK_MAX_PAGES triggers it.
 *
 * Usage:
 *   stackgrowtest            -- run all tests, print PASS/FAIL
 *   stackgrowtest -v         -- verbose: print per-test depth reached
 */

#include "types.h"
#include "stat.h"
#include "auxv6/user.h"
#include "signal.h"
#include "param.h"
#include "wait.h"

#define FRAME_PAD  512          /* local bytes burned per recursive frame    */
#define DEEP_TARGET 400         /* expected to require ~200 KiB stack        */
#define MAX_TARGET  ((USER_STACK_MAX_PAGES) * 4096 / FRAME_PAD + 50)

static int verbose = 0;
static int debug_mode = 0;
static volatile int caught_sigsegv = 0;
static volatile int sigsegv_depth  = -1;

static volatile uint deep_sp_start = 0;
static volatile uint deep_sp_min = 0;
static volatile uint max_sp_start = 0;
static volatile uint max_sp_min = 0;

/* Ensure the compiler does not tail-call-optimise away the recursion. */
static volatile int sink;

/* ------------------------------------------------------------------ */
/* Test 1: deep recursion                                              */
/* ------------------------------------------------------------------ */

/*
 * Each frame allocates a ~FRAME_PAD byte array to force real stack usage.
 * volatile prevents the compiler from eliding the alloca-equivalent.
 */
static int
deep_recurse(int depth, int target)
{
  volatile char pad[FRAME_PAD];
  int ret;
  uint sp;

  sp = (uint)&pad[0];
  if(depth == 0){
    deep_sp_start = sp;
    deep_sp_min = sp;
  }
  if(sp < deep_sp_min)
    deep_sp_min = sp;
  if(debug_mode && (depth % 64) == 0)
    dprintf(1, "  [d] deep depth=%d sp=0x%x used=%u bytes\n",
            depth, sp, deep_sp_start - deep_sp_min);

  /* Touch the first and last byte so the page fault fires. */
  pad[0]           = (char)depth;
  pad[FRAME_PAD-1] = (char)depth;
  sink = pad[0] + pad[FRAME_PAD-1];

  if(depth >= target)
    return depth;

  ret = deep_recurse(depth + 1, target);
  return ret;
}

static int
test_deep_recursion(void)
{
  int reached;

  if(verbose)
    dprintf(1, "  recursing to depth %d (frame=%d bytes, "
               "expected stack ~%d KiB)...\n",
            DEEP_TARGET, FRAME_PAD,
            (DEEP_TARGET * FRAME_PAD) / 1024);

  reached = deep_recurse(0, DEEP_TARGET);

  if(debug_mode)
    dprintf(1, "  [d] deep recurse stack span ~= %u bytes (%u KiB)\n",
            deep_sp_start - deep_sp_min,
            (deep_sp_start - deep_sp_min) / 1024);

  if(reached == DEEP_TARGET){
    dprintf(1, "PASS test_deep_recursion: reached depth %d\n", reached);
    return 1;
  }
  dprintf(1, "FAIL test_deep_recursion: only reached depth %d of %d\n",
          reached, DEEP_TARGET);
  return 0;
}

/* ------------------------------------------------------------------ */
/* Test 2: fork inherits grown stack bounds                            */
/* ------------------------------------------------------------------ */

static int
test_fork_inherit(void)
{
  int pid, r;

  /* Grow the stack in the parent first. */
  deep_recurse(0, DEEP_TARGET / 2);

  pid = fork();
  if(pid < 0){
    dprintf(1, "FAIL test_fork_inherit: fork failed\n");
    return 0;
  }
  if(pid == 0){
    /* Child: recurse deeply to verify stack bounds were inherited. */
    int reached = deep_recurse(0, DEEP_TARGET);
    if(reached == DEEP_TARGET)
      exit(0);
    exit(1);
  }

  /* Parent: wait for child. */
  r = 0;
  waitpid(pid, &r, 0);
  if(r == 0){
    dprintf(1, "PASS test_fork_inherit: child recursion succeeded\n");
    return 1;
  }
  dprintf(1, "FAIL test_fork_inherit: child exited with status %d\n", r);
  return 0;
}

/* ------------------------------------------------------------------ */
/* Test 3: SIGSEGV delivered when stack exceeds max                   */
/* ------------------------------------------------------------------ */

static void
segv_handler(int sig)
{
  (void)sig;
  caught_sigsegv = 1;
  /* Restore default so further faults terminate the child cleanly. */
  signal(SIGSEGV, SIG_DFL);
  exit(42);
}

/*
 * Attempt to recurse deep enough to exceed USER_STACK_MAX_PAGES.
 * Each frame consumes FRAME_PAD bytes; MAX_TARGET frames should reliably
 * exceed the ceiling even with some pad from the initial stack pages.
 */
static int
exhaust_recurse(int depth, int target)
{
  volatile char pad[FRAME_PAD];
  int ret;
  uint sp;

  sp = (uint)&pad[0];
  if(depth == 0){
    max_sp_start = sp;
    max_sp_min = sp;
  }
  if(sp < max_sp_min)
    max_sp_min = sp;
  if(debug_mode && (depth % 64) == 0)
    dprintf(1, "  [d] max depth=%d sp=0x%x used=%u bytes\n",
            depth, sp, max_sp_start - max_sp_min);

  pad[0]           = (char)depth;
  pad[FRAME_PAD-1] = (char)depth;
  sink = pad[0];

  if(depth >= target)
    return depth;

  // Keep a post-call use to block tail-call optimization; we need real
  // frame growth for this regression test.
  ret = exhaust_recurse(depth + 1, target);
  sink ^= (ret & 1);
  return ret;
}

static int
test_max_exceeded(void)
{
  int pid, r;
  struct sigaction sa;

  pid = fork();
  if(pid < 0){
    dprintf(1, "FAIL test_max_exceeded: fork failed\n");
    return 0;
  }

  if(pid == 0){
    /* Child: install SIGSEGV handler and try to exceed the stack limit. */
    sa.sa_handler = segv_handler;
    sa.sa_mask    = 0;
    sa.sa_flags   = 0;
    sigaction(SIGSEGV, &sa, 0);

    if(debug_mode)
      dprintf(1, "  [d] max test target depth=%d frame=%d bytes\n",
              MAX_TARGET, FRAME_PAD);

    exhaust_recurse(0, MAX_TARGET);

    if(debug_mode)
      dprintf(1, "  [d] max recurse unexpectedly returned; stack span ~= %u bytes (%u KiB)\n",
              max_sp_start - max_sp_min,
              (max_sp_start - max_sp_min) / 1024);

    /* If we somehow survived, exit with a distinct code. */
    exit(0);
  }

  /* Parent: child should have exited 42 via the SIGSEGV handler, or
   * been killed by SIGSEGV (exit status would be non-zero). */
  r = 0;
  waitpid(pid, &r, 0);
  if(debug_mode){
    if(WIFEXITED(r))
      dprintf(1, "  [d] child exited normally status=%d raw=0x%x\n",
              WEXITSTATUS(r), r);
    else if(WIFSIGNALED(r))
      dprintf(1, "  [d] child signaled sig=%d raw=0x%x\n",
              WTERMSIG(r), r);
    else
      dprintf(1, "  [d] child wait status raw=0x%x\n", r);
  }
  if(r != 0){
    dprintf(1, "PASS test_max_exceeded: child terminated on stack overflow "
               "(exit=%d)\n", r);
    return 1;
  }
  dprintf(1, "FAIL test_max_exceeded: child completed without SIGSEGV "
             "(stack limit not enforced)\n");
  return 0;
}

/* ------------------------------------------------------------------ */
/* main                                                                */
/* ------------------------------------------------------------------ */

int
main(int argc, char *argv[])
{
  int pass = 0, total = 0;
  int i;

  for(i = 1; i < argc; i++){
    if(strcmp(argv[i], "-v") == 0)
      verbose = 1;
    else if(strcmp(argv[i], "-d") == 0){
      debug_mode = 1;
      verbose = 1;
    }
  }

  dprintf(1, "stackgrowtest: USER_STACK_PAGES=%d "
             "USER_STACK_MAX_PAGES=%d\n",
          USER_STACK_PAGES, USER_STACK_MAX_PAGES);

  total++; if(test_deep_recursion()) pass++;
  total++; if(test_fork_inherit())   pass++;
  total++; if(test_max_exceeded())   pass++;

  dprintf(1, "stackgrowtest: %d/%d tests passed\n", pass, total);
  exit(pass == total ? 0 : 1);
}
