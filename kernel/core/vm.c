#include "param.h"
#include "types.h"
#include "defs.h"
#include "x86.h"
#include "memlayout.h"
#include "mmu.h"
#include "proc.h"
#include "elf.h"
#include "fs.h"
#include "vfs.h"

extern char data[];  // defined by kernel.ld
pde_t *kpgdir;  // for use in scheduler()
static uint vm_bad_pte_drops;
static uint vm_kernel_pde_repairs;
static uint vm_kernel_pde_master_repairs;
static uint vm_bad_entry_window_logs;
static uint vm_kernel_pde_sync_cursor;
static uint vm_kernel_pde_sync_calls;
static uint vm_kernel_pde_sync_full_calls;
static uint vm_kernel_pde_sync_entries;
static pde_t vm_kernel_pde_ref[NPDENTRIES];
static int vm_kernel_pde_ref_ready;
static uchar vm_kernel_pde_master_reported[NPDENTRIES];

static pde_t vm_pde_stable(pde_t pde);

static void
vm_log_proc_stack_context(const char *tag, void *entry_page)
{
  struct proc *p;
  uint kbase;
  uint ktop;
  uint ep;

  p = myproc();
  if(p == 0){
    cprintf("%s: proc=none\n", tag);
    return;
  }

  kbase = (uint)p->kstack;
  ktop = kbase + KSTACKSIZE;
  ep = (uint)entry_page;
  cprintf("%s: pid=%d pgdir=%p kstack=[%p,%p) entry_page=%p tf=%p frame=%p\n",
          tag, p->pid, p->pgdir, (void*)kbase, (void*)ktop,
          (void*)ep, p->tf, __builtin_frame_address(0));
  if(ep + PGSIZE == kbase || ktop + PGSIZE == ep || (ep >= kbase && ep < ktop)){
    cprintf("%s: stack_pgdir_proximity_detected pid=%d entry_page=%p kstack_base=%p\n",
            tag, p->pid, (void*)ep, (void*)kbase);
  }
}

static void
vm_dump_entry_window(uint *entry, const char *tag)
{
  uint base;
  uint idx;
  uint lo;
  uint hi;
  uint i;

  if(entry == 0)
    return;
  // Keep this bounded; this runs only on malformed-entry paths.
  if(vm_bad_entry_window_logs >= 32)
    return;
  vm_bad_entry_window_logs++;

  base = (uint)entry & ~(PGSIZE - 1);
  idx = ((uint)entry - base) / sizeof(uint);
  lo = (idx >= 4) ? (idx - 4) : 0;
  hi = (idx + 4 < NPTENTRIES) ? (idx + 4) : (NPTENTRIES - 1);

  cprintf("%s: entry=%p page=%p idx=%u cpu=%d cr3=%x\n",
          tag, entry, (void*)base, idx, cpuid(), rcr3());
  vm_log_proc_stack_context(tag, (void*)base);
  for(i = lo; i <= hi; i++)
    cprintf("  [%u]=%x\n", i, ((uint*)base)[i]);
}

static void
vm_log_kernel_pde_divergence_once(uint idx, pde_t cur, pde_t want, pde_t *pgdir)
{
  uint pcs[8];
  int j;

  if(idx >= NPDENTRIES)
    return;
  if(vm_kernel_pde_master_reported[idx])
    return;

  vm_kernel_pde_master_reported[idx] = 1;
  getcallerpcs(&pgdir, pcs);
  cprintf("vm_pde_diverge: idx=%u va=%p cur=%x want=%x stable_cur=%x stable_want=%x cpu=%d cr3=%x pgdir=%p\n",
          idx, (void*)(idx << PDXSHIFT), cur, want,
          vm_pde_stable(cur), vm_pde_stable(want), cpuid(), rcr3(), pgdir);
  cprintf("vm_pde_diverge_pcs:");
  for(j = 0; j < 8; j++)
    cprintf(" %p", pcs[j]);
  cprintf("\n");
}

// x86 sets Accessed/Dirty in paging entries at runtime.
#define VM_PDE_VOLATILE_BITS (0x020 | 0x040)
#define VM_PDE_SYNC_FULL_INTERVAL 64

static pde_t
vm_pde_stable(pde_t pde)
{
  return pde & ~VM_PDE_VOLATILE_BITS;
}

static pde_t
vm_pde_merge_runtime_bits(pde_t cur, pde_t want)
{
  return (want & ~VM_PDE_VOLATILE_BITS) | (cur & VM_PDE_VOLATILE_BITS);
}

static pde_t
vm_kernel_pde_canonical(uint idx)
{
  if(vm_kernel_pde_ref_ready)
    return vm_kernel_pde_ref[idx];
  if(kpgdir)
    return kpgdir[idx];
  return 0;
}

static void
vm_capture_kernel_pde_ref(void)
{
  uint i;

  if(kpgdir == 0)
    return;
  for(i = PDX(KERNBASE); i < NPDENTRIES; i++)
    vm_kernel_pde_ref[i] = kpgdir[i];
  vm_kernel_pde_ref_ready = 1;
}

static void
vm_sync_kernel_pdes(pde_t *pgdir)
{
  uint i;
  uint start;
  uint end;
  uint span;
  uint repaired;
  uint master_repaired;
  pde_t want;

  if(pgdir == 0 || kpgdir == 0)
    return;

  vm_kernel_pde_sync_calls++;

  span = NPDENTRIES - PDX(KERNBASE);
  if(span == 0)
    return;

  if(!vm_kernel_pde_ref_ready ||
     (vm_kernel_pde_sync_cursor % VM_PDE_SYNC_FULL_INTERVAL) == 0){
    start = PDX(KERNBASE);
    end = NPDENTRIES;
    vm_kernel_pde_sync_full_calls++;
  } else {
    start = PDX(KERNBASE) + (vm_kernel_pde_sync_cursor % span);
    end = start + 1;
  }
  vm_kernel_pde_sync_cursor++;
  vm_kernel_pde_sync_entries += (end - start);

  repaired = 0;
  master_repaired = 0;
  for(i = start; i < end; i++){
    want = vm_kernel_pde_canonical(i);
    if(vm_pde_stable(kpgdir[i]) != vm_pde_stable(want)){
      vm_log_kernel_pde_divergence_once(i, kpgdir[i], want, pgdir);
      kpgdir[i] = vm_pde_merge_runtime_bits(kpgdir[i], want);
      master_repaired++;
    }
    if(pgdir != kpgdir && vm_pde_stable(pgdir[i]) != vm_pde_stable(want)){
      pgdir[i] = vm_pde_merge_runtime_bits(pgdir[i], want);
      repaired++;
    }
  }

  if(master_repaired > 0){
    vm_kernel_pde_master_repairs += master_repaired;
    if((vm_kernel_pde_master_repairs & 0x3f) == master_repaired){
      cprintf("vm_sync_kernel_pdes: repaired_master=%u total_master=%u\n",
              master_repaired, vm_kernel_pde_master_repairs);
    }
  }

  if(repaired > 0){
    vm_kernel_pde_repairs += repaired;
    if((vm_kernel_pde_repairs & 0x3f) == repaired){
      cprintf("vm_sync_kernel_pdes: repaired=%u total=%u pgdir=%p\n",
              repaired, vm_kernel_pde_repairs, pgdir);
    }
  }
}

void
vm_get_sync_stats(uint *sync_calls,
                  uint *sync_full_calls,
                  uint *sync_entries,
                  uint *pgdir_repairs,
                  uint *master_repairs,
                  uint *bad_pte_drops)
{
  if(sync_calls)
    *sync_calls = vm_kernel_pde_sync_calls;
  if(sync_full_calls)
    *sync_full_calls = vm_kernel_pde_sync_full_calls;
  if(sync_entries)
    *sync_entries = vm_kernel_pde_sync_entries;
  if(pgdir_repairs)
    *pgdir_repairs = vm_kernel_pde_repairs;
  if(master_repairs)
    *master_repairs = vm_kernel_pde_master_repairs;
  if(bad_pte_drops)
    *bad_pte_drops = vm_bad_pte_drops;
}

// Set up CPU's kernel segment descriptors.
// Run once on entry on each CPU.
void
seginit(void)
{
  struct cpu *c;

  // Map "logical" addresses to virtual addresses using identity map.
  // Cannot share a CODE descriptor for both kernel and user
  // because it would have to have DPL_USR, but the CPU forbids
  // an interrupt from CPL=0 to DPL=3.
  c = &cpus[cpuid()];
  c->gdt[SEG_KCODE] = SEG(STA_X|STA_R, 0, 0xffffffff, 0);
  c->gdt[SEG_KDATA] = SEG(STA_W, 0, 0xffffffff, 0);
  c->gdt[SEG_UCODE] = SEG(STA_X|STA_R, 0, 0xffffffff, DPL_USER);
  c->gdt[SEG_UDATA] = SEG(STA_W, 0, 0xffffffff, DPL_USER);
  lgdt(c->gdt, sizeof(c->gdt));
  segreload();
}

// Return the address of the PTE in page table pgdir
// that corresponds to virtual address va.  If alloc!=0,
// create any required page table pages.
static pte_t *
walkpgdir(pde_t *pgdir, const void *va, int alloc)
{
  pde_t *pde;
  pte_t *pgtab;
  uint pa;

  pde = &pgdir[PDX(va)];
  if(*pde & PTE_P){
    pa = PTE_ADDR(*pde);
    if(pa == 0 || pa >= PHYSTOP || pa >= KERNBASE){
      cprintf("walkpgdir: bad pde pgdir=%p va=%p pde=%p raw=%x pa=%x\n",
              pgdir, va, pde, *pde, pa);
      vm_dump_entry_window((uint*)pde, "walkpgdir_bad_pde_window");
      if(!alloc){
        // Kernel-half PDEs are canonical and shared; repair drift in place.
        if((uint)va >= KERNBASE && kpgdir != 0 && PDX(va) >= PDX(KERNBASE)){
          *pde = vm_kernel_pde_canonical(PDX(va));
          if((*pde & PTE_P) != 0){
            pa = PTE_ADDR(*pde);
            if(pa != 0 && pa < PHYSTOP && pa < KERNBASE){
              pgtab = (pte_t*)P2V(pa);
              return &pgtab[PTX(va)];
            }
          }
        }
        return 0;
      }
      *pde = 0;
      pgtab = (pte_t*)kalloc();
      if(pgtab == 0)
        return 0;
      memset(pgtab, 0, PGSIZE);
      *pde = V2P(pgtab) | PTE_P | PTE_W | PTE_U;
    } else {
      pgtab = (pte_t*)P2V(pa);
    }
  } else {
    if(!alloc || (pgtab = (pte_t*)kalloc()) == 0)
      return 0;
    // Make sure all those PTE_P bits are zero.
    memset(pgtab, 0, PGSIZE);
    // The permissions here are overly generous, but they can
    // be further restricted by the permissions in the page table
    // entries, if necessary.
    *pde = V2P(pgtab) | PTE_P | PTE_W | PTE_U;
  }
  return &pgtab[PTX(va)];
}

static void
pte_assert_sane(uint pte)
{
  // Forward-compat invariant: present mappings cannot be both writable and COW.
  if((pte & PTE_P) && pte_is_cow(pte) && pte_is_writable(pte))
    panic("pte sane");
}

// Create PTEs for virtual addresses starting at va that refer to
// physical addresses starting at pa. va and size might not
// be page-aligned.
static int
mappages(pde_t *pgdir, void *va, uint size, uint pa, int perm)
{
  char *a, *last;
  pte_t *pte;

  a = (char*)PGROUNDDOWN((uint)va);
  last = (char*)PGROUNDDOWN(((uint)va) + size - 1);
  for(;;){
    if((pte = walkpgdir(pgdir, a, 1)) == 0)
      return -1;
    if(*pte & PTE_P)
      panic("remap");
    *pte = pa | perm | PTE_P;
    if(a == last)
      break;
    a += PGSIZE;
    pa += PGSIZE;
  }
  return 0;
}

// There is one page table per process, plus one that's used when
// a CPU is not running any process (kpgdir). The kernel uses the
// current process's page table during system calls and interrupts;
// page protection bits prevent user code from using the kernel's
// mappings.
//
// setupkvm() and exec() set up every page table like this:
//
//   0..KERNBASE: user memory (text+data+stack+heap), mapped to
//                phys memory allocated by the kernel
//   KERNBASE..KERNBASE+EXTMEM: mapped to 0..EXTMEM (for I/O space)
//   KERNBASE+EXTMEM..data: mapped to EXTMEM..V2P(data)
//                for the kernel's instructions and r/o data
//   data..KERNBASE+PHYSTOP: mapped to V2P(data)..PHYSTOP,
//                                  rw data + free physical memory
//   0xfe000000..0: mapped direct (devices such as ioapic)
//
// The kernel allocates physical memory for its heap and for user memory
// between V2P(end) and the end of physical memory (PHYSTOP)
// (directly addressable from end..P2V(PHYSTOP)).

// This table defines the kernel's mappings, which are present in
// every process's page table.
static struct kmap {
  void *virt;
  uint phys_start;
  uint phys_end;
  int perm;
} kmap[] = {
 { (void*)KERNBASE, 0,             EXTMEM,    PTE_W}, // I/O space
 { (void*)KERNLINK, V2P(KERNLINK), V2P(data), 0},     // kern text+rodata
 { (void*)data,     V2P(data),     PHYSTOP,   PTE_W}, // kern data+memory
 { (void*)DEVSPACE, DEVSPACE,      0,         PTE_W}, // more devices
};

// Set up kernel part of a page table.
pde_t*
setupkvm(void)
{
  pde_t *pgdir;
  struct kmap *k;
  uint i;

  if((pgdir = (pde_t*)kalloc()) == 0)
    return 0;
  memset(pgdir, 0, PGSIZE);

  // Modern model: all process page tables share canonical kernel-half PDEs.
  if(kpgdir != 0){
    for(i = PDX(KERNBASE); i < NPDENTRIES; i++)
      pgdir[i] = vm_kernel_pde_canonical(i);
    return pgdir;
  }

  if (P2V(PHYSTOP) > (void*)DEVSPACE)
    panic("PHYSTOP too high");
  for(k = kmap; k < &kmap[NELEM(kmap)]; k++)
    if(mappages(pgdir, k->virt, k->phys_end - k->phys_start,
                (uint)k->phys_start, k->perm) < 0) {
      freevm(pgdir);
      return 0;
    }
  return pgdir;
}

// Allocate one page table for the machine for the kernel address
// space for scheduler processes.
void
kvmalloc(void)
{
  kpgdir = setupkvm();
  vm_capture_kernel_pde_ref();
  switchkvm();
}

// Switch h/w page table register to the kernel-only page table,
// for when no process is running.
void
switchkvm(void)
{
  vm_sync_kernel_pdes(kpgdir);
  lcr3(V2P(kpgdir));   // switch to the kernel page table
}

// Switch TSS and h/w page table to correspond to process p.
void
switchuvm(struct proc *p)
{
  struct cpu *c;

  if(p == 0)
    panic("switchuvm: no process");
  if(p->kstack == 0)
    panic("switchuvm: no kstack");
  if(p->pgdir == 0)
    panic("switchuvm: no pgdir");

  // Repair any drift in shared kernel-half PDEs before loading CR3.
  vm_sync_kernel_pdes(p->pgdir);

  pushcli();
  c = mycpu();
  c->gdt[SEG_TSS] = SEG16(STS_T32A, &c->ts,
                          sizeof(c->ts)-1, 0);
  c->gdt[SEG_TSS].s = 0;
  c->ts.ss0 = SEG_KDATA << 3;
  c->ts.esp0 = (uint)p->kstack + KSTACKSIZE;
  // setting IOPL=0 in eflags *and* iomb beyond the tss segment limit
  // forbids I/O instructions (e.g., inb and outb) from user space
  c->ts.iomb = (ushort)0xFFFF;
  ltr(SEG_TSS << 3);
  lcr3(V2P(p->pgdir));  // switch to process's address space
  popcli();
}

// Load the initcode into address 0 of pgdir.
// sz must be less than a page.
void
inituvm(pde_t *pgdir, char *init, uint sz)
{
  char *mem;

  if(sz >= PGSIZE)
    panic("inituvm: more than a page");
  mem = kalloc();
  memset(mem, 0, PGSIZE);
  mappages(pgdir, 0, PGSIZE, V2P(mem), PTE_W|PTE_U);
  memmove(mem, init, sz);
}

// Load a program segment into pgdir.  addr must be page-aligned
// and the pages from addr to addr+sz must already be mapped.
int
loaduvm(pde_t *pgdir, char *addr, struct inode *ip, uint offset, uint sz)
{
  uint i, pa, n;
  pte_t *pte;
  const struct vnode_ops *ops;

  if((uint) addr % PGSIZE != 0)
    panic("loaduvm: addr must be page aligned");
  ops = vfs_dev_vops(inode_get_dev(ip));
  for(i = 0; i < sz; i += PGSIZE){
    if((pte = walkpgdir(pgdir, addr+i, 0)) == 0)
      panic("loaduvm: address should exist");
    pa = PTE_ADDR(*pte);
    if(sz - i < PGSIZE)
      n = sz - i;
    else
      n = PGSIZE;
    if(ops && ops->read){
      if(ops->read(ip, P2V(pa), offset+i, n) != n)
        return -1;
    } else {
      if(readi(ip, P2V(pa), offset+i, n) != n)
        return -1;
    }
  }
  return 0;
}

// Allocate page tables and physical memory to grow process from oldsz to
// newsz, which need not be page aligned.  Returns new size or 0 on error.
int
allocuvm(pde_t *pgdir, uint oldsz, uint newsz)
{
  char *mem;
  uint a;

  if(newsz >= KERNBASE)
    return 0;
  if(newsz < oldsz)
    return oldsz;

  a = PGROUNDUP(oldsz);
  for(; a < newsz; a += PGSIZE){
    mem = kalloc();
    if(mem == 0){
      cprintf("allocuvm out of memory\n");
      deallocuvm(pgdir, newsz, oldsz);
      return 0;
    }
    memset(mem, 0, PGSIZE);
    if(mappages(pgdir, (char*)a, PGSIZE, V2P(mem), PTE_W|PTE_U) < 0){
      cprintf("allocuvm out of memory (2)\n");
      deallocuvm(pgdir, newsz, oldsz);
      kfree(mem);
      return 0;
    }
  }
  return newsz;
}

// Deallocate user pages to bring the process size from oldsz to
// newsz.  oldsz and newsz need not be page-aligned, nor does newsz
// need to be less than oldsz.  oldsz can be larger than the actual
// process size.  Returns the new process size.
int
deallocuvm(pde_t *pgdir, uint oldsz, uint newsz)
{
  pte_t *pte;
  int rel;
  int bad;
  uint a;

  if(pgdir == 0)
    panic("deallocuvm: no pgdir");
  if(((uint)pgdir % PGSIZE) != 0){
    cprintf("deallocuvm: unaligned pgdir=%p oldsz=%u newsz=%u\n",
            pgdir, oldsz, newsz);
    panic("deallocuvm pgdir align");
  }
  if((uint)pgdir < KERNBASE || !kaddr_writable_current_pgdir((char*)pgdir)){
    cprintf("deallocuvm: unmapped pgdir=%p oldsz=%u newsz=%u\n",
            pgdir, oldsz, newsz);
    panic("deallocuvm pgdir map");
  }

  if(newsz >= oldsz)
    return oldsz;

  bad = 0;
  a = PGROUNDUP(newsz);
  for(; a  < oldsz; a += PGSIZE){
    pte = walkpgdir(pgdir, (char*)a, 0);
    if(!pte)
      a = PGADDR(PDX(a) + 1, 0, 0) - PGSIZE;
    else if((*pte & PTE_P) != 0){
      rel = uvm_release_pte(pte);
      if(rel == -2){
        bad++;
        continue;
      }
      if(rel < 0){
        bad++;
        *pte = 0;
        continue;
      }
    }
  }
  if(bad > 0)
    cprintf("deallocuvm: dropped %d bad user ptes pgdir=%p oldsz=%u newsz=%u\n",
            bad, pgdir, oldsz, newsz);
  return newsz;
}

// Free a page table and all the physical memory pages
// in the user part.
void
freevm(pde_t *pgdir)
{
  uint i;
  uint raw;
  uint pa;

  if(pgdir == 0)
    panic("freevm: no pgdir");
  deallocuvm(pgdir, KERNBASE, 0);
  for(i = 0; i < PDX(KERNBASE); i++){
    if(pgdir[i] & PTE_P){
      raw = pgdir[i];
      pa = PTE_ADDR(raw);
      if(pa == 0 || pa >= PHYSTOP || pa >= KERNBASE){
        cprintf("freevm: skip bad pde pgdir=%p idx=%u raw=%x pa=%x\n",
                pgdir, i, raw, pa);
        pgdir[i] = 0;
        continue;
      }
      kfree(P2V(pa));
      pgdir[i] = 0;
    }
  }
  kfree((char*)pgdir);
}

// Clear PTE_U on a page. Used to create an inaccessible
// page beneath the user stack.
void
clearpteu(pde_t *pgdir, char *uva)
{
  pte_t *pte;

  pte = walkpgdir(pgdir, uva, 0);
  if(pte == 0)
    panic("clearpteu");
  pte_assert_sane(*pte);
  pte_mark_user((uint*)pte, 0);
}

// Set PTE_U on a page.  Used to make a pre-allocated, inaccessible
// guard/overflow page accessible when the user stack grows down into it.
void
setpteu(pde_t *pgdir, char *uva)
{
  pte_t *pte;

  pte = walkpgdir(pgdir, uva, 0);
  if(pte == 0)
    panic("setpteu");
  pte_assert_sane(*pte);
  pte_mark_user((uint*)pte, 1);
}

// Return mapping state for a user VA in the given page table:
// 0 = not present, 1 = present but !PTE_U, 2 = present and PTE_U.
int
user_page_state(pde_t *pgdir, char *uva)
{
  pte_t *pte;

  pte = walkpgdir(pgdir, uva, 0);
  if(pte == 0 || ((*pte & PTE_P) == 0))
    return 0;
  pte_assert_sane(*pte);
  if(pte_is_user(*pte))
    return 2;
  return 1;
}

int
kaddr_writable_current_pgdir(char *kva)
{
  pde_t *pgdir;
  pte_t *pte;
  struct proc *p;

  if((uint)kva < KERNBASE)
    return 0;

  p = myproc();
  if(p && p->pgdir)
    pgdir = p->pgdir;
  else
    pgdir = kpgdir;

  if(pgdir == 0){
    /*
     * Early boot runs before kvmalloc() publishes kpgdir. During that phase,
     * entrypgdir maps only the bootstrap high window [KERNBASE,
     * KERNBASE+BOOT_EARLY_PHYSTOP). Treat that range as writable so debug
     * guards do not create false bootloop panics before full VM bring-up.
     */
    if((uint)kva >= KERNBASE && (uint)kva < (KERNBASE + BOOT_EARLY_PHYSTOP))
      return 1;
    return 0;
  }

  pte = walkpgdir(pgdir, kva, 0);
  if(pte == 0 || ((*pte & PTE_P) == 0))
    return 0;
  pte_assert_sane(*pte);
  if((*pte & PTE_W) == 0)
    return 0;

  return 1;
}

int
pte_is_cow(uint pte)
{
  return (pte & PTE_COW) != 0;
}

int
pte_is_writable(uint pte)
{
  return (pte & PTE_W) != 0;
}

int
pte_is_user(uint pte)
{
  return (pte & PTE_U) != 0;
}

void
pte_mark_cow(uint *pte)
{
  if(pte == 0)
    panic("pte_mark_cow");
  *pte &= ~PTE_W;
  *pte |= PTE_COW;
}

void
pte_mark_writable(uint *pte)
{
  if(pte == 0)
    panic("pte_mark_writable");
  *pte |= PTE_W;
  *pte &= ~PTE_COW;
}

void
pte_mark_user(uint *pte, int enabled)
{
  if(pte == 0)
    panic("pte_mark_user");
  if(enabled)
    *pte |= PTE_U;
  else
    *pte &= ~PTE_U;
}

int
uvm_release_pte(uint *pte)
{
  uint pa;
  uint raw;

  if(pte == 0)
    return -1;
  if((*pte & PTE_P) == 0)
    return 0;

  raw = *pte;
  pa = PTE_ADDR(*pte);
  if(pa == 0){
    vm_bad_pte_drops++;
    if((vm_bad_pte_drops & 0x3f) == 1){
      cprintf("uvm_release_pte: drop zero-pa pte=%p raw=%x flags=%x drops=%u\n",
              pte, raw, PTE_FLAGS(raw), vm_bad_pte_drops);
    }
    *pte = 0;
    return -2;
  }
  if(pa >= PHYSTOP || pa >= KERNBASE || !kpage_is_managed(pa)){
    struct proc *p;

    vm_bad_pte_drops++;
    p = myproc();
    if((vm_bad_pte_drops & 0x3f) == 1){
      cprintf("uvm_release_pte: drop bad pte=%p raw=%x pa=%x flags=%x drops=%u pid=%d pgdir=%p\n",
              pte, raw, pa, PTE_FLAGS(raw), vm_bad_pte_drops,
              p ? p->pid : -1, p ? p->pgdir : 0);
      vm_dump_entry_window(pte, "uvm_release_bad_pte_window");
    }
    *pte = 0;
    return -2;
  }

  // Phase 3 scaffolding: release through allocator refcount path so
  // shared-page teardown is safe when COW mappings are introduced.
  kfree(P2V(pa));
  *pte = 0;
  return 1;
}

int
cow_fault(pde_t *pgdir, uint va)
{
  pte_t *pte;
  uint pa;
  uint flags;
  char *mem;

  va = PGROUNDDOWN(va);
  pte = walkpgdir(pgdir, (char*)va, 0);
  if(pte == 0 || ((*pte & PTE_P) == 0))
    return -1;
  pte_assert_sane(*pte);
  if(!pte_is_cow(*pte))
    return -1;

  pa = PTE_ADDR(*pte);
  if(pa == 0)
    return -1;
  flags = PTE_FLAGS(*pte);

  if(kpage_refcount(pa) > 1){
    mem = kalloc();
    if(mem == 0)
      return -1;
    memmove(mem, (char*)P2V(pa), PGSIZE);
    if(uvm_release_pte((uint*)pte) < 0){
      kfree(mem);
      return -1;
    }
    flags = (flags & ~PTE_COW) | PTE_W;
    *pte = V2P(mem) | flags | PTE_P;
  } else {
    pte_mark_writable((uint*)pte);
  }

  lcr3(V2P(pgdir));
  return 0;
}

// Given a parent process's page table, create a copy
// of it for a child.
pde_t*
copyuvm(pde_t *pgdir, uint sz)
{
  pde_t *d;
  pte_t *pte;
  uint pa, i, flags;
  char *mem;
  uint cow_flags;

  if((d = setupkvm()) == 0)
    return 0;
  for(i = 0; i < sz; i += PGSIZE){
    pte = walkpgdir(pgdir, (void *)i, 0);
    if(pte == 0 || ((*pte & PTE_P) == 0))
      continue;
    pte_assert_sane(*pte);
    pa = PTE_ADDR(*pte);
    flags = PTE_FLAGS(*pte);

    if((flags & PTE_U) && kpage_is_managed(pa)){
      // Phase 4 slice 1: writable user pages become read-only COW-shared.
      if(flags & PTE_W){
        cow_flags = (flags | PTE_COW) & ~PTE_W;
        kpage_incref(pa);
        if(mappages(d, (void*)i, PGSIZE, pa, cow_flags) < 0){
          // Undo ref bump if child map install fails.
          kfree(P2V(pa));
          goto bad;
        }
        continue;
      }

      // Phase 4 slice 2: safe sharing for read-only managed user pages.
      kpage_incref(pa);
      if(mappages(d, (void*)i, PGSIZE, pa, flags) < 0){
        kfree(P2V(pa));
        goto bad;
      }
      continue;
    }

    if((mem = kalloc()) == 0)
      goto bad;
    memmove(mem, (char*)P2V(pa), PGSIZE);
    if(mappages(d, (void*)i, PGSIZE, V2P(mem), flags) < 0) {
      kfree(mem);
      goto bad;
    }
  }

  // After child mapping succeeds, write-protect parent writable user pages.
  for(i = 0; i < sz; i += PGSIZE){
    pte = walkpgdir(pgdir, (void *)i, 0);
    if(pte == 0 || ((*pte & PTE_P) == 0))
      continue;
    if((*pte & PTE_U) && (*pte & PTE_W) && kpage_is_managed(PTE_ADDR(*pte)))
      pte_mark_cow((uint*)pte);
  }
  return d;

bad:
  freevm(d);
  return 0;
}

//PAGEBREAK!
// Map user virtual address to kernel address.
char*
uva2ka(pde_t *pgdir, char *uva)
{
  pte_t *pte;

  pte = walkpgdir(pgdir, uva, 0);
  if(pte == 0)
    return 0;
  pte_assert_sane(*pte);
  if((*pte & PTE_P) == 0)
    return 0;
  if(!pte_is_user(*pte))
    return 0;
  return (char*)P2V(PTE_ADDR(*pte));
}

// Copy len bytes from p to user address va in page table pgdir.
// Most useful when pgdir is not the current page table.
// uva2ka ensures this only works for PTE_U pages.
int
copyout(pde_t *pgdir, uint va, void *p, uint len)
{
  char *buf, *pa0;
  pte_t *pte;
  uint n, va0;

  buf = (char*)p;
  while(len > 0){
    va0 = (uint)PGROUNDDOWN(va);
    pte = walkpgdir(pgdir, (char*)va0, 0);
    if(pte == 0 || ((*pte & PTE_P) == 0))
      return -1;
    pte_assert_sane(*pte);
    if(!pte_is_user(*pte))
      return -1;

    if(!pte_is_writable(*pte)){
      if(pte_is_cow(*pte)){
        if(cow_fault(pgdir, va0) < 0)
          return -1;
        pte = walkpgdir(pgdir, (char*)va0, 0);
        if(pte == 0 || ((*pte & PTE_P) == 0))
          return -1;
        pte_assert_sane(*pte);
        if(!pte_is_user(*pte) || !pte_is_writable(*pte))
          return -1;
      } else {
        return -1;
      }
    }

    pa0 = (char*)P2V(PTE_ADDR(*pte));
    if(pa0 == 0)
      return -1;
    n = PGSIZE - (va - va0);
    if(n > len)
      n = len;
    memmove(pa0 + (va - va0), buf, n);
    len -= n;
    buf += n;
    va = va0 + PGSIZE;
  }
  return 0;
}

// Copy len bytes from user address va in page table pgdir to p.
// Most useful when pgdir is not the current page table.
// uva2ka ensures this only works for PTE_U pages.
int
copyin(pde_t *pgdir, void *p, uint va, uint len)
{
  char *buf, *pa0;
  uint n, va0;

  buf = (char*)p;
  while(len > 0){
    va0 = (uint)PGROUNDDOWN(va);
    pa0 = uva2ka(pgdir, (char*)va0);
    if(pa0 == 0)
      return -1;
    n = PGSIZE - (va - va0);
    if(n > len)
      n = len;
    memmove(buf, pa0 + (va - va0), n);
    len -= n;
    buf += n;
    va = va0 + PGSIZE;
  }
  return 0;
}

//PAGEBREAK!
// Blank page.
//PAGEBREAK!
// Blank page.
//PAGEBREAK!
// Blank page.

