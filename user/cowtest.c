#include "types.h"
#include "sys/stat.h"
#include "param.h"
#include "auxv6/user.h"

#define COWTEST_PROFILE "2026-04-11-r1"

#define DEFAULT_ROUNDS 8

#define DATA_BYTES   (2 * 4096)
#define HEAP_BYTES   (2 * 4096)
#define STACK_BYTES  1024

static char global_region[DATA_BYTES];

static int passed;
static int failed;

static uchar
pattern_byte(uchar seed, int index)
{
  uint value;

  value = (uint)seed;
  value += (uint)(index * 37);
  value += (uint)((index >> 3) * 13);
  value ^= (uint)(index * 7);
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

static int
verify_all_regions(const char *phase, char *heap_region, char *stack_region,
                   uchar seed)
{
  if(verify_pattern(phase, "data", global_region, sizeof(global_region), seed) < 0)
    return -1;
  if(verify_pattern(phase, "heap", heap_region, HEAP_BYTES, seed) < 0)
    return -1;
  if(verify_pattern(phase, "stack", stack_region, STACK_BYTES, seed) < 0)
    return -1;
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
read_token(int fd, char *token)
{
  return read(fd, token, 1) == 1 ? 0 : -1;
}

static int
write_token(int fd, char token)
{
  return write(fd, &token, 1) == 1 ? 0 : -1;
}

static int
run_child_round(int round, int child_to_parent, int parent_to_child,
                char *heap_region, char *stack_region,
                uchar base_seed, uchar child_seed)
{
  char token;

  if(verify_all_regions("child inherited view", heap_region, stack_region,
                        base_seed) < 0)
    return 10 + round;

  fill_all_regions(heap_region, stack_region, child_seed);
  if(verify_all_regions("child post-write view", heap_region, stack_region,
                        child_seed) < 0)
    return 20 + round;

  if(write_token(child_to_parent, 'C') < 0)
    return 30 + round;
  if(read_token(parent_to_child, &token) < 0)
    return 40 + round;
  if(token != 'P')
    return 50 + round;

  if(verify_all_regions("child preserved view", heap_region, stack_region,
                        child_seed) < 0)
    return 60 + round;

  return 0;
}

static int
run_round(int round, char *heap_region, char *stack_region)
{
  int child_to_parent[2];
  int parent_to_child[2];
  int pid;
  int st;
  char token;
  uchar base_seed;
  uchar child_seed;
  uchar parent_seed;

  base_seed = (uchar)(0x11 + round * 3);
  child_seed = (uchar)(0x47 + round * 5);
  parent_seed = (uchar)(0x93 + round * 7);

  fill_all_regions(heap_region, stack_region, base_seed);
  if(pipe(child_to_parent) < 0)
    return -1;
  if(pipe(parent_to_child) < 0){
    close(child_to_parent[0]);
    close(child_to_parent[1]);
    return -1;
  }

  pid = fork();
  if(pid < 0){
    close(child_to_parent[0]);
    close(child_to_parent[1]);
    close(parent_to_child[0]);
    close(parent_to_child[1]);
    return -1;
  }

  if(pid == 0){
    int rc;

    close(child_to_parent[0]);
    close(parent_to_child[1]);
    rc = run_child_round(round, child_to_parent[1], parent_to_child[0],
                         heap_region, stack_region, base_seed, child_seed);
    close(child_to_parent[1]);
    close(parent_to_child[0]);
    exit(rc);
  }

  close(child_to_parent[1]);
  close(parent_to_child[0]);

  if(read_token(child_to_parent[0], &token) < 0 || token != 'C'){
    close(child_to_parent[0]);
    close(parent_to_child[1]);
    waitpid(pid, &st, 0);
    return -1;
  }

  if(verify_all_regions("parent preserved base", heap_region, stack_region,
                        base_seed) < 0){
    write_token(parent_to_child[1], 'F');
    close(child_to_parent[0]);
    close(parent_to_child[1]);
    waitpid(pid, &st, 0);
    return -1;
  }

  fill_all_regions(heap_region, stack_region, parent_seed);
  if(verify_all_regions("parent post-write view", heap_region, stack_region,
                        parent_seed) < 0){
    write_token(parent_to_child[1], 'F');
    close(child_to_parent[0]);
    close(parent_to_child[1]);
    waitpid(pid, &st, 0);
    return -1;
  }

  if(write_token(parent_to_child[1], 'P') < 0){
    close(child_to_parent[0]);
    close(parent_to_child[1]);
    waitpid(pid, &st, 0);
    return -1;
  }

  close(child_to_parent[0]);
  close(parent_to_child[1]);

  if(waitpid(pid, &st, 0) != pid)
    return -1;
  if(!WIFEXITED(st) || WEXITSTATUS(st) != 0){
    dprintf(1, "[DIAG] round %d child exit status=0x%x\n", round + 1, st);
    return -1;
  }

  if(verify_all_regions("parent final view", heap_region, stack_region,
                        parent_seed) < 0)
    return -1;

  return 0;
}

static void
usage(void)
{
  dprintf(2, "usage: cowtest [-r rounds]\n");
}

int
main(int argc, char *argv[])
{
  int i;
  int rounds;
  char *heap_region;
  char stack_region[STACK_BYTES];

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
    dprintf(2, "cowtest: invalid round count\n");
    exit(1);
  }

  heap_region = sbrk(HEAP_BYTES);
  if(heap_region == (char*)-1){
    dprintf(2, "cowtest: sbrk failed\n");
    exit(1);
  }

  dprintf(1,
          "cowtest: profile=%s rounds=%d data=%d heap=%d stack=%d\n",
          COWTEST_PROFILE, rounds, DATA_BYTES, HEAP_BYTES, STACK_BYTES);

  for(i = 0; i < rounds; i++){
    if(run_round(i, heap_region, stack_region) < 0){
      dprintf(1, "[FAIL] round %d: parent/child COW isolation failed\n", i + 1);
      failed++;
    } else {
      dprintf(1, "[PASS] round %d\n", i + 1);
      passed++;
    }
  }

  dprintf(1, "cowtest: passed=%d failed=%d\n", passed, failed);
  exit(failed ? 1 : 0);
}