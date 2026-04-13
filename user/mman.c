#include "errno.h"
#include "stdlib.h"
#include "string.h"
#include "sys/mman.h"

#define MMAP_SLOTS 128

struct mmap_slot {
  void *addr;
  size_t len;
  int used;
};

static struct mmap_slot g_mmap_slots[MMAP_SLOTS];

static int
mmap_slot_find(void *addr)
{
  int i;

  for(i = 0; i < MMAP_SLOTS; i++) {
    if(g_mmap_slots[i].used && g_mmap_slots[i].addr == addr)
      return i;
  }

  return -1;
}

static int
mmap_slot_alloc(void)
{
  int i;

  for(i = 0; i < MMAP_SLOTS; i++) {
    if(!g_mmap_slots[i].used)
      return i;
  }

  return -1;
}

void *
mmap(void *addr, size_t len, int prot, int flags, int fd, off_t off)
{
  int slot;
  void *p;

  (void)prot;

  if(len == 0) {
    errno = EINVAL;
    return MAP_FAILED;
  }

  if(addr != 0) {
    errno = ENOSYS;
    return MAP_FAILED;
  }

  if((flags & MAP_FIXED) != 0) {
    errno = ENOSYS;
    return MAP_FAILED;
  }

  if((flags & MAP_ANONYMOUS) != 0) {
    if(fd != -1 || off != 0) {
      errno = EINVAL;
      return MAP_FAILED;
    }
  } else {
    errno = ENOSYS;
    return MAP_FAILED;
  }

  if((flags & (MAP_PRIVATE | MAP_SHARED)) == 0) {
    errno = EINVAL;
    return MAP_FAILED;
  }

  slot = mmap_slot_alloc();
  if(slot < 0) {
    errno = ENOMEM;
    return MAP_FAILED;
  }

  p = malloc(len);
  if(p == 0) {
    errno = ENOMEM;
    return MAP_FAILED;
  }

  memset(p, 0, len);
  g_mmap_slots[slot].addr = p;
  g_mmap_slots[slot].len = len;
  g_mmap_slots[slot].used = 1;
  return p;
}

int
munmap(void *addr, size_t len)
{
  int slot;

  if(addr == 0 || len == 0) {
    errno = EINVAL;
    return -1;
  }

  slot = mmap_slot_find(addr);
  if(slot < 0) {
    errno = EINVAL;
    return -1;
  }

  if(g_mmap_slots[slot].len != len) {
    errno = EINVAL;
    return -1;
  }

  free(g_mmap_slots[slot].addr);
  g_mmap_slots[slot].addr = 0;
  g_mmap_slots[slot].len = 0;
  g_mmap_slots[slot].used = 0;
  return 0;
}

int
mprotect(void *addr, size_t len, int prot)
{
  (void)addr;
  (void)len;
  (void)prot;
  errno = ENOSYS;
  return -1;
}

int
msync(void *addr, size_t len, int flags)
{
  (void)addr;
  (void)len;
  (void)flags;
  errno = ENOSYS;
  return -1;
}

int
mlock(const void *addr, size_t len)
{
  (void)addr;
  (void)len;
  errno = ENOSYS;
  return -1;
}

int
munlock(const void *addr, size_t len)
{
  (void)addr;
  (void)len;
  errno = ENOSYS;
  return -1;
}

int
madvise(void *addr, size_t len, int advice)
{
  (void)addr;
  (void)len;
  (void)advice;
  errno = ENOSYS;
  return -1;
}
