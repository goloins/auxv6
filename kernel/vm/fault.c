#include "types.h"
#include "defs.h"
#include "param.h"
#include "memlayout.h"
#include "mmu.h"
#include "proc.h"
#include "signal.h"
#include "traps.h"
#include "vma.h"
#include "x86.h"

static uint vm_fault_dispatches;
static uint vm_fault_cow_resolved;
static uint vm_fault_stack_growth;
static uint vm_fault_demand_zero;
static uint vm_fault_sigsegv;

int
cow_fault(pde_t *pgdir, uint va)
{
  pte_t *pte;
  uint pa;
  uint flags;
  char *mem;

  va = PGROUNDDOWN(va);
  pte = (pte_t*)vm_lookup_pte(pgdir, va);
  if(pte == 0 || ((*pte & PTE_P) == 0))
    return -1;
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

  vm_tlb_flush(pgdir);
  return 0;
}

int
proc_try_grow_stack(struct proc *p, uint fault_addr)
{
  uint stack_guard;
  uint pages_used;
  int pst;

  if(p->stack_top == 0 || p->stack_bot == 0)
    return 0;
  if(p->stack_bot >= p->stack_top)
    return 0;

  stack_guard = p->stack_bot - PGSIZE;

  if(fault_addr < stack_guard || fault_addr >= p->stack_bot)
    return 0;

  pages_used = (p->stack_top - p->stack_bot) / PGSIZE;
  if(pages_used >= USER_STACK_MAX_PAGES) {
    STACKDBG("stack: pid %d tried to grow beyond max (%d pages)\n",
             p->pid, USER_STACK_MAX_PAGES);
    return 0;
  }

  pst = user_page_state(proc_pgdir(p), (char*)stack_guard);
  if(pst == 1){
    setpteu(proc_pgdir(p), (char*)stack_guard);
  } else if(pst == 0){
    if(p->addrsp){
      if(allocuvm_as(p->addrsp, stack_guard, stack_guard + PGSIZE) == 0)
        return 0;
    } else {
      if(allocuvm(proc_pgdir(p), stack_guard, stack_guard + PGSIZE) == 0)
        return 0;
    }
  } else {
    return 0;
  }

  p->stack_bot = stack_guard;

  STACKDBG("stack: pid %d grew stack to 0x%x (%d/%d pages used)\n",
           p->pid, p->stack_bot, pages_used + 1, USER_STACK_MAX_PAGES);

  switchuvm(p);
  return 1;
}

static int
vm_fault_is_user_write_protect(struct trapframe *tf)
{
  if(tf == 0)
    return 0;
  return (tf->err & 0x3) == 0x3;
}

static int
vm_fault_is_user_not_present(struct trapframe *tf)
{
  if(tf == 0)
    return 0;
  return (tf->err & 0x5) == 0x4;
}

static int
fault_cow_resolve(struct trapframe *tf, struct proc *p, uint fault_addr)
{
  if(tf == 0 || p == 0)
    return -1;
  if(!vm_fault_is_user_write_protect(tf))
    return -1;
  return cow_fault(proc_pgdir(p), fault_addr);
}

static int
fault_stack_growth(struct proc *p, uint fault_addr)
{
  if(p == 0)
    return 0;
  return proc_try_grow_stack(p, fault_addr);
}

static int
fault_demand_zero(struct proc *p, uint fault_addr)
{
  struct vaddr_range *vma;
  uint *pte;
  uint va;
  int writable;

  if(p == 0 || p->addrsp == 0)
    return -1;

  va = PGROUNDDOWN(fault_addr);
  vma = vma_find(p->addrsp, va);
  if(vma == 0)
    return -1;
  if((vma->flags & VMA_ZEROFILL) == 0)
    return -1;
  if(vma->inode != 0)
    return -1;

  pte = vm_lookup_pte(proc_pgdir(p), va);
  if(pte != 0 && ((*pte & PTE_P) != 0))
    return -1;

  writable = (vma->flags & VMA_WRITE) != 0;
  if(vm_map_zerofill_page(p->addrsp, va, writable) < 0)
    return -1;

  vm_tlb_flush(proc_pgdir(p));
  return 0;
}

void
vm_get_fault_stats(uint *dispatches, uint *cow_resolved,
                   uint *stack_growth,
                   uint *demand_zero,
                   uint *sigsegv)
{
  if(dispatches)
    *dispatches = vm_fault_dispatches;
  if(cow_resolved)
    *cow_resolved = vm_fault_cow_resolved;
  if(stack_growth)
    *stack_growth = vm_fault_stack_growth;
  if(demand_zero)
    *demand_zero = vm_fault_demand_zero;
  if(sigsegv)
    *sigsegv = vm_fault_sigsegv;
}

int
vm_handle_fault(struct trapframe *tf, uint fault_addr)
{
  struct proc *p;

  p = myproc();
  if(tf == 0 || p == 0)
    return -1;

  vm_fault_dispatches++;
  if(vm_fault_is_user_write_protect(tf)){
    if(fault_cow_resolve(tf, p, fault_addr) == 0){
      vm_fault_cow_resolved++;
      return 0;
    }
  }

  if(fault_stack_growth(p, fault_addr)){
    vm_fault_stack_growth++;
    return 0;
  }

  if(vm_fault_is_user_not_present(tf)){
    if(fault_demand_zero(p, fault_addr) == 0){
      vm_fault_demand_zero++;
      return 0;
    }
  }

  p->sig_pending |= SIGBIT(SIGSEGV);
  vm_fault_sigsegv++;
  return -1;
}