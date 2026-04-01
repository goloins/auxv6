/*
 * losetup - Loop device setup utility
 *
 * Usage:
 *   losetup                       - List all loop devices
 *   losetup /dev/loopN /path/to/file   - Setup loop device
 *   losetup -d /dev/loopN         - Detach loop device
 */

#include "../include/types.h"
#include "../include/stat.h"
#include "../include/user.h"
#include "../include/fcntl.h"

#define LOOP_DEV_BASE 40
#define NLOOP 8

static void
print_usage(void)
{
  printf(2, "Usage:\n");
  printf(2, "  losetup                           - list all loop devices\n");
  printf(2, "  losetup /dev/loopN /path/to/file  - setup loop device N with file\n");
  printf(2, "  losetup -d /dev/loopN             - detach loop device N\n");
  printf(2, "  losetup -f /path/to/file          - setup first free loop with file\n");
}

static int
parse_loop_num(char *dev)
{
  /* Expect /dev/loopN or loopN */
  char *p = dev;
  int num;
  
  if(strncmp(p, "/dev/", 5) == 0)
    p += 5;
  
  if(strncmp(p, "loop", 4) != 0){
    printf(2, "losetup: invalid device '%s', expected /dev/loopN\n", dev);
    return -1;
  }
  
  p += 4;
  num = atoi(p);
  
  if(num < 0 || num >= NLOOP){
    printf(2, "losetup: loop number %d out of range (0-%d)\n", num, NLOOP - 1);
    return -1;
  }
  
  return num;
}

static void
list_loops(void)
{
  int i;
  uint inum, offset, nblocks, flags;
  int active_count = 0;
  
  printf(1, "NAME       ACTIVE  BLOCKS  OFFSET  MOUNTED  INODE\n");
  
  for(i = 0; i < NLOOP; i++){
    int status = loopstatus(i, &inum, &offset, &nblocks, &flags);
    
    if(status < 0){
      printf(2, "/dev/loop%d: error getting status\n", i);
      continue;
    }
    
    if(status > 0){
      printf(1, "/dev/loop%d  yes     %d      %d       %s      %d\n",
             i, nblocks, offset,
             (flags & LOOP_STATUS_MOUNTED) ? "yes" : "no",
             inum);
      active_count++;
    } else {
      printf(1, "/dev/loop%d  no      -       -       -        -\n", i);
    }
  }
  
  printf(1, "\n%d of %d loop devices in use\n", active_count, NLOOP);
}

static int
setup_loop(int loopnum, char *file)
{
  int r;
  struct stat st;
  
  /* Check file exists */
  if(stat(file, &st) < 0){
    printf(2, "losetup: cannot stat '%s'\n", file);
    return -1;
  }
  
  if(st.st_type != T_FILE){
    printf(2, "losetup: '%s' is not a regular file\n", file);
    return -1;
  }
  
  /* Setup the loop device */
  r = loopsetup(loopnum, file, 0, 0);  /* offset=0, nblocks=auto */
  
  if(r < 0){
    printf(2, "losetup: failed to setup /dev/loop%d with '%s'\n", loopnum, file);
    return -1;
  }
  
  printf(1, "/dev/loop%d: set up with '%s' (%d bytes)\n", loopnum, file, st.st_size);
  
  /* Create the device node if it doesn't exist */
  char devpath[16];
  devpath[0] = '/'; devpath[1] = 'd'; devpath[2] = 'e'; devpath[3] = 'v';
  devpath[4] = '/'; devpath[5] = 'l'; devpath[6] = 'o'; devpath[7] = 'o';
  devpath[8] = 'p'; devpath[9] = '0' + loopnum; devpath[10] = 0;
  
  if(stat(devpath, &st) < 0){
    if(mknod(devpath, M_IFBLK | 0660, 2, LOOP_DEV_BASE + loopnum) < 0){
      printf(2, "losetup: warning: could not create %s device node\n", devpath);
    }
  }
  
  return 0;
}

static int
detach_loop(int loopnum)
{
  int r = loopteardown(loopnum);
  
  if(r < 0){
    printf(2, "losetup: failed to detach /dev/loop%d\n", loopnum);
    return -1;
  }
  
  printf(1, "/dev/loop%d: detached\n", loopnum);
  return 0;
}

int
main(int argc, char *argv[])
{
  int loopnum;
  
  if(argc == 1){
    /* List all loop devices */
    list_loops();
    exit();
  }
  
  if(argc == 2 && strcmp(argv[1], "-h") == 0){
    print_usage();
    exit();
  }
  
  if(argc == 3 && strcmp(argv[1], "-d") == 0){
    /* Detach loop device */
    loopnum = parse_loop_num(argv[2]);
    if(loopnum < 0)
      exit();
    detach_loop(loopnum);
    exit();
  }
  
  if(argc == 3 && strcmp(argv[1], "-f") == 0){
    /* Find first free loop device */
    uint inum, offset, nblocks, flags;
    for(loopnum = 0; loopnum < NLOOP; loopnum++){
      int status = loopstatus(loopnum, &inum, &offset, &nblocks, &flags);
      if(status == 0)
        break;
    }
    if(loopnum >= NLOOP){
      printf(2, "losetup: no free loop devices available\n");
      exit();
    }
    setup_loop(loopnum, argv[2]);
    exit();
  }
  
  if(argc == 3){
    /* Setup loop device */
    loopnum = parse_loop_num(argv[1]);
    if(loopnum < 0)
      exit();
    setup_loop(loopnum, argv[2]);
    exit();
  }
  
  print_usage();
  exit();
}
