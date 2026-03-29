#include "types.h"
#include "user.h"
#include "fcntl.h"

static struct mountinfo g_mounts[MOUNTINFO_MAX];
static int g_nmounts;

static const struct mountinfo*
find_mount_for_dev(int dev)
{
  int i;

  for(i = 0; i < g_nmounts; i++){
    if(g_mounts[i].dev == dev)
      return &g_mounts[i];
  }
  return 0;
}

static void
print_one(char *name, int dev, char *kind)
{
  int blocks;
  const struct mountinfo *mi;

  blocks = devblocks(dev);
  if(blocks <= 0)
    return;

  mi = find_mount_for_dev(dev);
  if(mi)
    printf(1, "%-8s dev=%2d %-5s blocks=%6d mounted=%s type=%s\n",
           name, dev, kind, blocks, mi->path, mi->fstype);
  else
    printf(1, "%-8s dev=%2d %-5s blocks=%6d mounted=-\n",
           name, dev, kind, blocks);
}

int
main(int argc, char *argv[])
{
  int unit;
  int part;
  char name[10];

  if(argc != 1){
    printf(2, "usage: lsblk\n");
    exit();
  }

  g_nmounts = mountinfo(g_mounts, MOUNTINFO_MAX);
  if(g_nmounts < 0)
    g_nmounts = 0;

  printf(1, "NAME     DEV TYPE  BLOCKS  MOUNT\n");
  for(unit = 0; unit < HD_DISK_UNITS; unit++){
    name[0] = 'h';
    name[1] = 'd';
    name[2] = 'a' + unit;
    name[3] = 0;
    print_one(name, HD_DISK_DEV(unit), "disk");

    for(part = 1; part <= HD_PARTS_PER_DISK; part++){
      name[0] = 'h';
      name[1] = 'd';
      name[2] = 'a' + unit;
      name[3] = '0' + part;
      name[4] = 0;
      print_one(name, HD_PART_DEV(unit, part), "part");
    }
  }

  exit();
}
