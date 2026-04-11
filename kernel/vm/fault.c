#include "types.h"
#include "defs.h"
#include "param.h"
#include "mmu.h"
#include "proc.h"
#include "signal.h"
#include "traps.h"
#include "x86.h"

static uint vm_fault_dispatches;
static uint vm_fault_cow_resolved;
static uint vm_fault_stack_growth;
static uint vm_fault_sigsegv;

static int
vm_fault_is_user_write_protect(struct trapframe *tf)
{
  if(tf == 0)
    return 0;
  return (tf->err & 0x3) == 0x3;
}

void
vm_get_fault_stats(uint *dispatches, uint *cow_resolved,
                   uint *stack_growth, uint *sigsegv)
{
  if(dispatches)
    *dispatches = vm_fault_dispatches;
  if(cow_resolved)
    *cow_resolved = vm_fault_cow_resolved;
  if(stack_growth)
    *stack_growth = vm_fault_stack_growth;
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
    if(cow_fault(proc_pgdir(p), fault_addr) == 0){
      vm_fault_cow_resolved++;
      return 0;
    }
  }

  if(proc_try_grow_stack(p, fault_addr)){
    vm_fault_stack_growth++;
    return 0;
  }

  p->sig_pending |= SIGBIT(SIGSEGV);
  vm_fault_sigsegv++;
  return -1;
}