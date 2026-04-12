#ifndef _VMA_H_
#define _VMA_H_

#include "types.h"

struct inode;
struct address_space;

#define VMA_READ   0x0001
#define VMA_WRITE  0x0002
#define VMA_EXEC   0x0004
#define VMA_SHARED 0x0008
#define VMA_COW    0x0010

struct vaddr_range {
  uint va_start;
  uint va_end;
  uint flags;
  struct inode *inode;
  uint file_offset;
};

struct address_space {
  pde_t *pgdir;
  uint owns_pgdir;
  uint transitional;
  struct vaddr_range *vmas;
  uint vma_count;
  uint vma_cap;
  uint vm_size;
  uint rss;
};

#endif
