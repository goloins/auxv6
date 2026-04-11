#include "types.h"
#include "defs.h"
#include "vma.h"

static int
vma_ensure_capacity(struct address_space *as, uint need)
{
  struct vaddr_range *newv;
  uint newcap;

  if(as == 0)
    return -1;
  if(need <= as->vma_cap)
    return 0;

  newcap = (as->vma_cap == 0) ? 4 : as->vma_cap;
  while(newcap < need)
    newcap <<= 1;

  newv = (struct vaddr_range*)kmalloc(newcap * sizeof(*newv));
  if(newv == 0)
    return -1;

  if(as->vmas && as->vma_count)
    memmove(newv, as->vmas, as->vma_count * sizeof(*newv));
  if(as->vmas)
    kmalloc_free(as->vmas);

  as->vmas = newv;
  as->vma_cap = newcap;
  return 0;
}

static struct address_space*
address_space_alloc(void)
{
  struct address_space *as;

  as = (struct address_space*)kmalloc(sizeof(*as));
  if(as == 0)
    return 0;
  memset(as, 0, sizeof(*as));
  return as;
}

struct address_space*
address_space_create(void)
{
  struct address_space *as;

  as = address_space_alloc();
  if(as == 0)
    return 0;
  if(setupkvm_as(as) < 0){
    kmalloc_free(as);
    return 0;
  }
  as->owns_pgdir = 1;
  as->transitional = 0;

  return as;
}

struct address_space*
address_space_dup_cow(struct address_space *src)
{
  struct address_space *dst;
  uint i;

  if(src == 0)
    return 0;

  dst = address_space_alloc();
  if(dst == 0)
    return 0;

  dst->pgdir = copyuvm_as(src, src->vm_size);
  if(dst->pgdir == 0){
    kmalloc_free(dst);
    return 0;
  }
  dst->owns_pgdir = 1;
  dst->transitional = src->transitional;

  if(src->vma_count > 0){
    if(vma_ensure_capacity(dst, src->vma_count) < 0){
      freevm(dst->pgdir);
      kmalloc_free(dst);
      return 0;
    }
    for(i = 0; i < src->vma_count; i++)
      dst->vmas[i] = src->vmas[i];
    dst->vma_count = src->vma_count;
  }

  dst->vm_size = src->vm_size;
  dst->rss = src->rss;
  if(dst->vma_count > 0)
    dst->transitional = 0;
  return dst;
}

pde_t*
address_space_pgdir(struct address_space *as)
{
  if(as == 0)
    return 0;
  return as->pgdir;
}

void
address_space_release(struct address_space *as)
{
  address_space_destroy(as);
}

void
address_space_destroy(struct address_space *as)
{
  if(as == 0)
    return;
  if(as->vmas)
    kmalloc_free(as->vmas);
  if(as->owns_pgdir && as->pgdir)
    freevm(as->pgdir);
  kmalloc_free(as);
}

int
vma_expand_flags(struct address_space *as, uint new_size, uint flags)
{
  struct vaddr_range *last;
  struct vaddr_range vma;
  uint old_size;

  if(as == 0)
    return -1;
  if(new_size < as->vm_size)
    return -1;
  if(new_size == as->vm_size)
    return 0;

  old_size = as->vm_size;
  if(as->vma_count > 0){
    last = &as->vmas[as->vma_count - 1];
    if(last->va_end == old_size && last->flags == flags && last->inode == 0){
      last->va_end = new_size;
      as->vm_size = new_size;
      as->transitional = 0;
      return 0;
    }
  }

  memset(&vma, 0, sizeof(vma));
  vma.va_start = old_size;
  vma.va_end = new_size;
  vma.flags = flags;
  if(vma_insert(as, &vma) < 0)
    return -1;
  as->vm_size = new_size;
  if(as->vma_count > 0)
    as->transitional = 0;
  return 0;
}

int
vma_expand(struct address_space *as, uint new_size)
{
  return vma_expand_flags(as, new_size, VMA_READ | VMA_WRITE);
}

int
vma_shrink(struct address_space *as, uint new_size)
{
  struct vaddr_range *vma;

  if(as == 0)
    return -1;
  if(new_size > as->vm_size)
    return -1;
  if(new_size == as->vm_size)
    return 0;

  while(as->vma_count > 0){
    vma = &as->vmas[as->vma_count - 1];
    if(vma->va_start >= new_size){
      vma_remove(as, vma);
      continue;
    }
    if(vma->va_end > new_size)
      vma->va_end = new_size;
    break;
  }
  as->vm_size = new_size;
  return 0;
}

struct vaddr_range*
vma_find(struct address_space *as, uint va)
{
  uint i;

  if(as == 0)
    return 0;

  for(i = 0; i < as->vma_count; i++){
    if(va >= as->vmas[i].va_start && va < as->vmas[i].va_end)
      return &as->vmas[i];
  }

  return 0;
}

int
vma_insert(struct address_space *as, const struct vaddr_range *vma)
{
  uint i;
  uint pos;

  if(as == 0 || vma == 0 || vma->va_start >= vma->va_end)
    return -1;

  pos = as->vma_count;
  for(i = 0; i < as->vma_count; i++){
    if(vma->va_end <= as->vmas[i].va_start){
      pos = i;
      break;
    }
    if(!(vma->va_start >= as->vmas[i].va_end))
      return -1;
  }

  if(vma_ensure_capacity(as, as->vma_count + 1) < 0)
    return -1;

  for(i = as->vma_count; i > pos; i--)
    as->vmas[i] = as->vmas[i - 1];
  as->vmas[pos] = *vma;
  as->vma_count++;
  as->transitional = 0;
  return 0;
}

void
vma_remove(struct address_space *as, struct vaddr_range *vma)
{
  uint i;

  if(as == 0 || vma == 0 || as->vma_count == 0)
    return;

  for(i = 0; i < as->vma_count; i++){
    if(&as->vmas[i] == vma)
      break;
  }
  if(i == as->vma_count)
    return;

  for(; i + 1 < as->vma_count; i++)
    as->vmas[i] = as->vmas[i + 1];
  as->vma_count--;
}
