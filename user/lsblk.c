#include "types.h"
#include "auxv6/user.h"
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

static char*
find_field(char *line, const char *key)
{
  int keylen;
  char *p;

  if(!line || !key)
    return 0;
  keylen = strlen(key);
  p = line;
  while(*p){
    if(strncmp(p, key, keylen) == 0)
      return p + keylen;
    while(*p && *p != ' ' && *p != '\n')
      p++;
    while(*p == ' ')
      p++;
    if(*p == '\n')
      p++;
  }
  return 0;
}

static int
is_atapi_dev(int dev)
{
  char buf[512];
  int fd;
  int n;
  char *line;

  fd = open("/proc/ahci_tune", O_RDONLY);
  if(fd < 0)
    return 0;
  n = read(fd, buf, sizeof(buf) - 1);
  close(fd);
  if(n <= 0)
    return 0;

  buf[n] = 0;
  line = buf;
  while(*line){
    char *next;
    char *p;
    int v;

    next = line;
    while(*next && *next != '\n')
      next++;
    if(*next == '\n')
      *next++ = 0;

    if(strncmp(line, "hba=", 4) == 0){
      p = find_field(line, "type=");
      if(p && strncmp(p, "atapi", 5) == 0){
        p = find_field(line, "dev=");
        if(p){
          v = atoi(p);
          if(v == dev)
            return 1;
        }
      }
    }

    line = next;
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
    dprintf(1, "%-8s dev=%2d %-5s blocks=%6d mounted=%s type=%s\n",
            name, dev, kind, blocks, mi->path, mi->fstype);
  else
    dprintf(1, "%-8s dev=%2d %-5s blocks=%6d mounted=-\n",
            name, dev, kind, blocks);
}

static void
print_family(char prefix, int units, int parts_per_disk)
{
  int unit;
  int part;
  int dev;
  char name[10];

  for(unit = 0; unit < units; unit++){
    name[0] = prefix;
    name[1] = 'd';
    name[2] = 'a' + unit;
    name[3] = 0;

    if(prefix == 'h')
      dev = HD_DISK_DEV(unit);
    else if(prefix == 'v')
      dev = VD_DISK_DEV(unit);
    else
      dev = ND_DISK_DEV(unit);
    if(prefix == 'h' && is_atapi_dev(dev))
      continue;
    print_one(name, dev, "disk");

    for(part = 1; part <= parts_per_disk; part++){
      name[0] = prefix;
      name[1] = 'd';
      name[2] = 'a' + unit;
      name[3] = '0' + part;
      name[4] = 0;

      if(prefix == 'h')
        dev = HD_PART_DEV(unit, part);
      else
        dev = VD_PART_DEV(unit, part);
      if(prefix == 'h' && is_atapi_dev(dev))
        continue;
      print_one(name, dev, "part");
    }
  }
}

static void
print_cdroms(void)
{
  char buf[512];
  int fd;
  int n;
  char *line;
  int idx;

  fd = open("/proc/ahci_tune", O_RDONLY);
  if(fd < 0)
    return;
  n = read(fd, buf, sizeof(buf) - 1);
  close(fd);
  if(n <= 0)
    return;

  buf[n] = 0;
  line = buf;
  idx = 0;
  while(*line){
    char *next;
    char *p;
    int v;
    char name[10];

    next = line;
    while(*next && *next != '\n')
      next++;
    if(*next == '\n')
      *next++ = 0;

    if(strncmp(line, "hba=", 4) == 0){
      p = find_field(line, "type=");
      if(p && strncmp(p, "atapi", 5) == 0){
        p = find_field(line, "dev=");
        if(p){
          v = atoi(p);
          if(idx == 0)
            strcpy(name, "cdrom");
          else {
            strcpy(name, "cdrom0");
            name[5] = '0' + idx;
            name[6] = 0;
          }
          print_one(name, v, "rom");
          idx++;
        }
      }
    }

    line = next;
  }
}

int
main(int argc, char *argv[])
{
  if(argc != 1){
    dprintf(2, "usage: lsblk\n");
    exit(1);
  }

  g_nmounts = mountinfo(g_mounts, MOUNTINFO_MAX);
  if(g_nmounts < 0)
    g_nmounts = 0;

  dprintf(1, "NAME     DEV TYPE  BLOCKS  MOUNT\n");
  print_family('h', HD_DISK_UNITS, HD_PARTS_PER_DISK);
  print_cdroms();
  print_family('v', VD_DISK_UNITS, VD_PARTS_PER_DISK);
  print_family('n', ND_DISK_UNITS, 0);

  exit(0);
}
