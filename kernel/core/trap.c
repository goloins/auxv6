#include "types.h"
#include "defs.h"
#include "param.h"
#include "memlayout.h"
#include "mmu.h"
#include "proc.h"
#include "x86.h"
#include "traps.h"
#include "spinlock.h"
#include "signal.h"

#define MAX_IRQ (256 - T_IRQ0)

// Interrupt descriptor table (shared by all CPUs).
struct gatedesc idt[256];
extern uint vectors[];  // in vectors.S: array of 256 entry pointers
struct spinlock tickslock;
uint ticks;

void
tvinit(void)
{
  int i;

  for(i = 0; i < 256; i++)
    SETGATE(idt[i], 0, SEG_KCODE<<3, vectors[i], 0);
  SETGATE(idt[T_SYSCALL], 1, SEG_KCODE<<3, vectors[T_SYSCALL], DPL_USER);

  initlock(&tickslock, "time");
  lockdep_set_rank(&tickslock, LOCK_RANK_TICKS, "ticks");
  ktime_init();
  irq_init();  // Initialize dynamic IRQ handlers
}

void
idtinit(void)
{
  lidt(idt, sizeof(idt));
}

//PAGEBREAK: 41
void
trap(struct trapframe *tf)
{
  if(tf->trapno == T_SYSCALL){
    if(myproc()->killed)
      exit(0);
    myproc()->tf = tf;
    syscall();
    proc_handle_signals_on_return(myproc());
    if(myproc()->killed)
      exit(0);
    return;
  }

  switch(tf->trapno){
  case T_DBLFLT:
    trap_kernel_fatal(tf, "double-fault");
    break;
  case T_IRQ0 + IRQ_TIMER:
    if(cpuid() == 0){
      uint current_ticks;

      acquire(&tickslock);
      ticks++;
      current_ticks = ticks;
      release(&tickslock);
      if(proc_has_tick_sleepers())
        wakeup(&ticks);
      ktime_tick(current_ticks);
      // Check all processes for expired alarms
      proc_check_alarms(current_ticks);
      // Update load averages every 500 ticks (5 seconds at 100Hz)
      if((current_ticks % 500) == 0)
        proc_tick_loadavg();
      // Poll network devices for RX/TX completions.
      netdev_poll();
      // TCP slow timer - every 10 ticks (100ms)
      if((current_ticks % 10) == 0)
        tcp_slowtimo();
    }
    // Charge one CPU tick to the process running on this CPU (all CPUs).
    if(myproc())
      myproc()->cticks++;
    lapiceoi();
    break;
  case T_IRQ0 + IRQ_IDE:
    ideintr();
    lapiceoi();
    break;
  case T_IRQ0 + IRQ_IDE+1:
    ideintr();
    lapiceoi();
    break;
  case T_IRQ0 + IRQ_KBD:
    kbdintr();
    lapiceoi();
    break;
  case T_IRQ0 + IRQ_MOUSE:
    mouseintr();
    lapiceoi();
    break;
  case T_IRQ0 + IRQ_COM1:
    uartintr();
    lapiceoi();
    break;
  case T_IRQ0 + 7:
  case T_IRQ0 + IRQ_SPURIOUS:
    cprintf("cpu%d: spurious interrupt at %x:%x\n",
            cpuid(), tf->cs, tf->eip);
    lapiceoi();
    break;

  //PAGEBREAK: 13
  case T_PGFLT:
    // Page fault: before delivering SIGSEGV, try to grow the user stack.
    // The faulting virtual address is in CR2.
    if(myproc() && (tf->cs & 3) == DPL_USER){
      uint fa = rcr2();
      // Phase 4 correctness: only resolve COW on user write-protection faults
      // (present=1, write=1 in x86 page-fault error code).
      if((tf->err & 0x3) == 0x3){
        if(cow_fault(myproc()->pgdir, fa) == 0)
          break;
      }
      if(proc_try_grow_stack(myproc(), fa))
        break;  // Stack grown; resume user instruction
      // Not a growable stack fault — fall through to signal delivery.
      myproc()->sig_pending |= SIGBIT(SIGSEGV);
    } else {
      // Kernel-mode page fault: always fatal.
      trap_kernel_fatal(tf, "kernel-page-fault");
    }
    break;

  default:
    // Check for dynamically registered IRQ handlers
    if(tf->trapno >= T_IRQ0 && tf->trapno < T_IRQ0 + MAX_IRQ){
      int irq = tf->trapno - T_IRQ0;
      if(irq_dispatch(irq)){
        lapiceoi();
        break;
      }
      // Unhandled IRQ - just acknowledge and continue
      lapiceoi();
      break;
    }
    
    if(myproc() == 0 || (tf->cs&3) == 0){
      // In kernel, it must be our mistake.
      trap_kernel_fatal(tf, "kernel-unexpected-trap");
    }
    // In user space, deliver appropriate signal for hardware faults.
    // If the process has a handler installed, it can catch it.
    {
      int signo = trap_to_signal(tf->trapno);
      if(signo) {
        // Post signal - will be delivered before returning to userspace
        myproc()->sig_pending |= SIGBIT(signo);
      } else {
        // Unknown trap, just kill
        cprintf("pid %d %s: trap %d err %d on cpu %d "
                "eip 0x%x addr 0x%x--kill proc\n",
                myproc()->pid, myproc()->name, tf->trapno,
                tf->err, cpuid(), tf->eip, rcr2());
        myproc()->killed = 1;
      }
    }
  }

  if(myproc() && (tf->cs&3) == DPL_USER) {
    // Keep p->tf synchronized on interrupt/trap return so signal-frame
    // delivery uses the current user register context.
    myproc()->tf = tf;
    proc_handle_signals_on_return(myproc());
  }

  // Force process exit if it has been killed and is in user space.
  // (If it is still executing in the kernel, let it keep running
  // until it gets to the regular system call return.)
  if(myproc() && myproc()->killed && (tf->cs&3) == DPL_USER)
    exit(0);

  // Force process to give up CPU on clock tick.
  // If interrupts were on while locks held, would need to check nlock.
  if(myproc() && myproc()->state == RUNNING &&
     tf->trapno == T_IRQ0+IRQ_TIMER)
    yield();

  // Check if the process has been killed since we yielded
  if(myproc() && myproc()->killed && (tf->cs&3) == DPL_USER)
    exit(0);
}
