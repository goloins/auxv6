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

// Dynamic IRQ handler table
// Indexed by IRQ number (0-15 for legacy PIC, 0-23 for IOAPIC)
// Supports shared interrupts with a chain of handlers
#define MAX_IRQ 24
#define MAX_HANDLERS_PER_IRQ 8

typedef void (*irq_handler_t)(int irq, void *arg);

struct shared_irq_handler {
  irq_handler_t handler;
  void         *arg;
  const char   *name;
};

struct irq_entry {
  struct shared_irq_handler handlers[MAX_HANDLERS_PER_IRQ];
  int num_handlers;
};

static struct irq_entry irq_handlers[MAX_IRQ];
static struct spinlock irq_lock;

// Initialize IRQ subsystem (called from tvinit)
static void
irq_init(void)
{
  initlock(&irq_lock, "irq");
  for(int i = 0; i < MAX_IRQ; i++){
    irq_handlers[i].num_handlers = 0;
    for(int j = 0; j < MAX_HANDLERS_PER_IRQ; j++){
      irq_handlers[i].handlers[j].handler = 0;
      irq_handlers[i].handlers[j].arg = 0;
      irq_handlers[i].handlers[j].name = 0;
    }
  }
}

// Register an IRQ handler
// Returns 0 on success, -1 if IRQ is full or invalid
int
irq_register(int irq, irq_handler_t handler, void *arg, const char *name)
{
  if(irq < 0 || irq >= MAX_IRQ || !handler)
    return -1;
  
  acquire(&irq_lock);
  
  struct irq_entry *entry = &irq_handlers[irq];
  
  // Check if we have a free slot
  if(entry->num_handlers >= MAX_HANDLERS_PER_IRQ){
    release(&irq_lock);
    cprintf("irq_register: IRQ %d handler table full (max %d)\n", 
            irq, MAX_HANDLERS_PER_IRQ);
    return -1;
  }
  
  // Add handler to the list
  int idx = entry->num_handlers;
  entry->handlers[idx].handler = handler;
  entry->handlers[idx].arg = arg;
  entry->handlers[idx].name = name;
  entry->num_handlers++;
  
  release(&irq_lock);
  
  BOOTDBG("irq: registered IRQ %d for %s (handler %d/%d)\n", 
          irq, name, idx+1, MAX_HANDLERS_PER_IRQ);
  return 0;
}

// Unregister an IRQ handler (by name)
// Returns 0 on success, -1 if not found
int
irq_unregister(int irq, const char *name)
{
  if(irq < 0 || irq >= MAX_IRQ || !name)
    return -1;
  
  acquire(&irq_lock);
  
  struct irq_entry *entry = &irq_handlers[irq];
  
  for(int i = 0; i < entry->num_handlers; i++){
    if(entry->handlers[i].name && strcmp(entry->handlers[i].name, name) == 0){
      // Remove by shifting remaining handlers down
      for(int j = i; j < entry->num_handlers - 1; j++){
        entry->handlers[j] = entry->handlers[j+1];
      }
      entry->num_handlers--;
      release(&irq_lock);
      return 0;
    }
  }
  
  release(&irq_lock);
  cprintf("irq_unregister: handler '%s' not found on IRQ %d\n", name, irq);
  return -1;
}

// Dispatch to all dynamic IRQ handlers registered for this IRQ
// Returns number of handlers called
static int
irq_dispatch(int irq)
{
  if(irq < 0 || irq >= MAX_IRQ)
    return 0;
  
  struct irq_entry *entry = &irq_handlers[irq];
  int handled = 0;
  
  // Call all registered handlers for this IRQ
  for(int i = 0; i < entry->num_handlers; i++){
    if(entry->handlers[i].handler){
      entry->handlers[i].handler(irq, entry->handlers[i].arg);
      handled++;
    }
  }
  
  return handled;
}

// Map x86 trap number to Unix signal number
// Returns 0 if no mapping (unknown trap)
static int
trap_to_signal(int trapno)
{
  switch(trapno) {
  case T_DIVIDE:   return SIGFPE;    // Divide by zero
  case T_DEBUG:    return SIGTRAP;   // Debug exception
  case T_BRKPT:    return SIGTRAP;   // Breakpoint (int3)
  case T_OFLOW:    return SIGFPE;    // Overflow
  case T_BOUND:    return SIGSEGV;   // Bounds check failed
  case T_ILLOP:    return SIGILL;    // Illegal opcode
  case T_DEVICE:   return SIGFPE;    // Device not available
  case T_GPFLT:    return SIGSEGV;   // General protection fault
  case T_PGFLT:    return SIGSEGV;   // Page fault
  case T_FPERR:    return SIGFPE;    // x87 FPU error
  case T_ALIGN:    return SIGBUS;    // Alignment check
  case T_SIMDERR:  return SIGFPE;    // SIMD floating point
  default:         return 0;         // Unknown
  }
}

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
  case T_IRQ0 + IRQ_TIMER:
    if(cpuid() == 0){
      uint current_ticks;

      acquire(&tickslock);
      ticks++;
      current_ticks = ticks;
      wakeup(&ticks);
      release(&tickslock);
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
      if(proc_try_grow_stack(myproc(), fa))
        break;  // Stack grown; resume user instruction
      // Not a growable stack fault — fall through to signal delivery.
      myproc()->sig_pending |= SIGBIT(SIGSEGV);
    } else {
      // Kernel-mode page fault: always fatal.
      cprintf("unexpected trap %d from cpu %d eip %x (cr2=0x%x)\n",
              tf->trapno, cpuid(), tf->eip, rcr2());
      panic("trap");
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
      cprintf("unexpected trap %d from cpu %d eip %x (cr2=0x%x)\n",
              tf->trapno, cpuid(), tf->eip, rcr2());
      panic("trap");
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

  if(myproc() && (tf->cs&3) == DPL_USER)
    proc_handle_signals_on_return(myproc());

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
