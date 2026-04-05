#include "types.h"
#include "defs.h"
#include "memlayout.h"
#include "fcntl.h"

#define MBOOT1_MAGIC         0x2BADB002U
#define MBOOT1_FLAG_CMDLINE  (1U << 2)

struct mboot1_info {
  uint flags;
  uint mem_lower;
  uint mem_upper;
  uint boot_device;
  uint cmdline;       /* phys addr of NUL-terminated cmdline string */
  /* remaining fields unused */
};

/* Written by entry.S at physical addresses before paging enable. */
uint mboot_magic_raw;
uint mboot_info_phys_raw;

/* Resolved root device; 0 means use compile-time ROOTFS_DEV. */
uint boot_rootdev;

static int
phys_addr_valid(uint pa)
{
  return pa != 0 && pa < PHYSTOP;
}

static uint
cmdline_parse_rootdev(const char *s)
{
  for(; *s; ){
    if(s[0]=='r' && s[1]=='o' && s[2]=='o' && s[3]=='t' && s[4]=='='){
      s += 5;
      if(s[0]=='/' && s[1]=='d' && s[2]=='e' && s[3]=='v' && s[4]=='/'){
        s += 5;
        if(s[0]=='h' && s[1]=='d' && s[2]>='a' && s[2]<'a'+HD_DISK_UNITS){
          int unit = s[2] - 'a';
          s += 3;
          if(*s >= '1' && *s <= '0' + HD_PARTS_PER_DISK)
            return HD_PART_DEV(unit, *s - '0');
          return HD_DISK_DEV(unit);
        }
      }
      return 0;
    }
    while(*s && *s != ' ') s++;
    while(*s == ' ') s++;
  }
  return 0;
}

void
multiboot_init(void)
{
  struct mboot1_info *info;

  boot_rootdev = 0;

  if(mboot_magic_raw != MBOOT1_MAGIC)
    return;

  if(!phys_addr_valid(mboot_info_phys_raw))
    return;

  info = (struct mboot1_info *)P2V(mboot_info_phys_raw);

  if(info->flags & MBOOT1_FLAG_CMDLINE){
    if(!phys_addr_valid(info->cmdline))
      return;
    const char *cmdline = (const char *)P2V(info->cmdline);
    boot_rootdev = cmdline_parse_rootdev(cmdline);
  }
}
