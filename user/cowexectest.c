#include "types.h"
#include "stat.h"
#include "param.h"
#include "auxv6/user.h"

#define COWEXECTEST_PROFILE "2026-04-11-r1"

#define DEFAULT_ROUNDS 8
#define DATA_BYTES   (2 * 4096)
#define HEAP_BYTES   (2 * 4096)
#define STACK_BYTES  1024
#define EXEC_HEAP_BYTES 4096
#define EXEC_STACK_BYTES 512

static char global_region[DATA_BYTES];

static int passed;
static int failed;

static uchar
pattern_byte(uchar seed, int index)
{
  uint value;

  value = (uint)seed;
  value += (uint)(index * 29);
  value ^= (uint)(index * 11);
  value += (uint)((index >> 2) * 7);
  return (uchar)value;
}

static void
fill_pattern(char *buf, int len, uchar seed)
{
  int i;

  for(i = 0; i < len; i++)
    buf[i] = (char)pattern_byte(seed, i);
}

static int
verify_pattern(const char *phase, const char *region, char *buf, int len, uchar seed)
{
  int i;
  uchar want;
  uchar got;

  for(i = 0; i < len; i++){
    want = pattern_byte(seed, i);
    got = (uchar)buf[i];
    if(got != want){
      dprintf(1,
              "[DIAG] %s %s mismatch at byte %d: got=0x%x want=0x%x\n",
              phase, region, i, got, want);
      return -1;
    }
  }

  return 0;
}

static void
fill_all_regions(char *heap_region, char *stack_region, uchar seed)
{
  fill_pattern(global_region, sizeof(global_region), seed);
  fill_pattern(heap_region, HEAP_BYTES, seed);
  fill_pattern(stack_region, STACK_BYTES, seed);
}

static int
verify_all_regions(const char *phase, char *heap_region, char *stack_region, uchar seed)
{
  if(verify_pattern(phase, "data", global_region, sizeof(global_region), seed) < 0)
    return -1;
  if(verify_pattern(phase, "heap", heap_region, HEAP_BYTES, seed) < 0)
    return -1;
  if(verify_pattern(phase, "stack", stack_region, STACK_BYTES, seed) < 0)
    return -1;
  return 0;
}

static int
read_token(int fd, char *token)
{
  return read(fd, token, 1) == 1 ? 0 : -1;
}

static int
write_token(int fd, char token)
{
  return write(fd, &token, 1) == 1 ? 0 : -1;
}

static void
format_int(char *buf, int value)
{
  char tmp[16];
  int i;
  int n;
  uint x;

  if(value == 0){
    buf[0] = '0';
    buf[1] = 0;
    return;
  }

  n = 0;
  x = (uint)value;
  while(x > 0 && n < (int)sizeof(tmp)){
    tmp[n++] = (char)('0' + (x % 10));
    x /= 10;
  }
  for(i = 0; i < n; i++)
    buf[i] = tmp[n - 1 - i];
  buf[n] = 0;
}

static int
exec_helper_main(int notify_fd, int round)
{
  char *heap_region;
  char stack_region[EXEC_STACK_BYTES];
  uchar seed;

  heap_region = sbrk(EXEC_HEAP_BYTES);
  if(heap_region == (char*)-1)
    return 2;

  seed = (uchar)(0xc1 + round * 9);
  fill_pattern(global_region, sizeof(global_region), seed);
  fill_pattern(heap_region, EXEC_HEAP_BYTES, seed);
  fill_pattern(stack_region, sizeof(stack_region), seed);

  if(verify_pattern("exec image", "data", global_region, sizeof(global_region), seed) < 0)
    return 3;
  if(verify_pattern("exec image", "heap", heap_region, EXEC_HEAP_BYTES, seed) < 0)
    return 4;
  if(verify_pattern("exec image", "stack", stack_region, sizeof(stack_region), seed) < 0)
    return 5;
  if(write_token(notify_fd, 'E') < 0)
    return 6;
  return 0;
}

static void
run_exec_helper_or_die(int notify_fd, int round)
{
  exit(exec_helper_main(notify_fd, round));
}

static void
exec_self_helper_or_die(int notify_fd, int round)
{
  char fd_buf[16];
  char round_buf[16];
  char *argv_exec[5];

  format_int(fd_buf, notify_fd);
  format_int(round_buf, round);
  argv_exec[0] = "cowexectest";
  argv_exec[1] = "--exec-child";
  argv_exec[2] = fd_buf;
  argv_exec[3] = round_buf;
  argv_exec[4] = 0;

  exec("/bin/cowexectest", argv_exec);
  exec("/cowexectest", argv_exec);
  dprintf(1, "[DIAG] exec helper launch failed for round %d\n", round + 1);
  exit(100 + round);
}

static int
run_round(int round, char *heap_region, char *stack_region)
{
  int child_to_parent[2];
  int pid;
  int st;
  char token;
  uchar base_seed;
  uchar child_seed;
  uchar parent_seed;

  base_seed = (uchar)(0x17 + round * 3);
  child_seed = (uchar)(0x53 + round * 5);
  parent_seed = (uchar)(0x91 + round * 7);

  fill_all_regions(heap_region, stack_region, base_seed);
  if(pipe(child_to_parent) < 0)
    return -1;

  pid = fork();
  if(pid < 0){
    close(child_to_parent[0]);
    close(child_to_parent[1]);
    return -1;
  }

  if(pid == 0){
    close(child_to_parent[0]);
    if(verify_all_regions("child inherited view", heap_region, stack_region, base_seed) < 0)
      exit(10 + round);
    fill_all_regions(heap_region, stack_region, child_seed);
    if(verify_all_regions("child post-write view", heap_region, stack_region, child_seed) < 0)
      exit(20 + round);
    if(write_token(child_to_parent[1], 'M') < 0)
      exit(30 + round);
    exec_self_helper_or_die(child_to_parent[1], round);
  }

  close(child_to_parent[1]);
  if(read_token(child_to_parent[0], &token) < 0 || token != 'M'){
    close(child_to_parent[0]);
    waitpid(pid, &st, 0);
    return -1;
  }

  if(verify_all_regions("parent preserved base", heap_region, stack_region, base_seed) < 0){
    close(child_to_parent[0]);
    waitpid(pid, &st, 0);
    return -1;
  }

  fill_all_regions(heap_region, stack_region, parent_seed);
  if(verify_all_regions("parent post-write view", heap_region, stack_region, parent_seed) < 0){
    close(child_to_parent[0]);
    waitpid(pid, &st, 0);
    return -1;
  }

  if(read_token(child_to_parent[0], &token) < 0 || token != 'E'){
    close(child_to_parent[0]);
    waitpid(pid, &st, 0);
    return -1;
  }

  close(child_to_parent[0]);
  if(waitpid(pid, &st, 0) != pid)
    return -1;
  if(!WIFEXITED(st) || WEXITSTATUS(st) != 0){
    dprintf(1, "[DIAG] round %d child exit status=0x%x\n", round + 1, st);
    return -1;
  }

  if(verify_all_regions("parent final view", heap_region, stack_region, parent_seed) < 0)
    return -1;
  return 0;
}

static void
usage(void)
{
  dprintf(2, "usage: cowexectest [-r rounds]\n");
}

int
main(int argc, char *argv[])
{
  int i;
  int rounds;
  char *heap_region;
  char stack_region[STACK_BYTES];

  if(argc == 4 && strcmp(argv[1], "--exec-child") == 0)
    run_exec_helper_or_die(atoi(argv[2]), atoi(argv[3]));

  rounds = DEFAULT_ROUNDS;
  for(i = 1; i < argc; i++){
    if(strcmp(argv[i], "-h") == 0 || strcmp(argv[i], "--help") == 0){
      usage();
      exit(0);
    }
    if(strcmp(argv[i], "-r") == 0 && i + 1 < argc){
      rounds = atoi(argv[++i]);
      continue;
    }
    usage();
    exit(1);
  }

  if(rounds < 1){
    dprintf(2, "cowexectest: invalid round count\n");
    exit(1);
  }

  heap_region = sbrk(HEAP_BYTES);
  if(heap_region == (char*)-1){
    dprintf(2, "cowexectest: sbrk failed\n");
    exit(1);
  }

  dprintf(1,
          "cowexectest: profile=%s rounds=%d data=%d heap=%d stack=%d\n",
          COWEXECTEST_PROFILE, rounds, DATA_BYTES, HEAP_BYTES, STACK_BYTES);

  for(i = 0; i < rounds; i++){
    if(run_round(i, heap_region, stack_region) < 0){
      dprintf(1, "[FAIL] round %d: fork+exec COW handoff failed\n", i + 1);
      failed++;
    } else {
      dprintf(1, "[PASS] round %d\n", i + 1);
      passed++;
    }
  }

  dprintf(1, "cowexectest: passed=%d failed=%d\n", passed, failed);
  exit(failed ? 1 : 0);
}