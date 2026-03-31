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
#define MAX_IRQ 24

typedef void (*irq_handler_t)(int irq, void *arg);

struct irq_entry {
  irq_handler_t handler;
  void         *arg;
  const char   *name;
};

static struct irq_entry irq_handlers[MAX_IRQ];
static struct spinlock irq_lock;

// Initialize IRQ subsystem (called from tvinit)
static void
irq_init(void)
{
  initlock(&irq_lock, "irq");
  for(int i = 0; i < MAX_IRQ; i++){
    irq_handlers[i].handler = 0;
    irq_handlers[i].arg = 0;
    irq_handlers[i].name = 0;
  }
}

// Register an IRQ handler
// Returns 0 on success, -1 if already registered
int
irq_register(int irq, irq_handler_t handler, void *arg, const char *name)
{
  if(irq < 0 || irq >= MAX_IRQ)
    return -1;
  
  acquire(&irq_lock);
  if(irq_handlers[irq].handler != 0){
    release(&irq_lock);
    cprintf("irq_register: IRQ %d already in use by %s\n", 
            irq, irq_handlers[irq].name);
    return -1;
  }
  irq_handlers[irq].handler = handler;
  irq_handlers[irq].arg = arg;
  irq_handlers[irq].name = name;
  release(&irq_lock);
  
  cprintf("irq: registered IRQ %d for %s\n", irq, name);
  return 0;
}

// Unregister an IRQ handler
void
irq_unregister(int irq)
{
  if(irq < 0 || irq >= MAX_IRQ)
    return;
  
  acquire(&irq_lock);
  irq_handlers[irq].handler = 0;
  irq_handlers[irq].arg = 0;
  irq_handlers[irq].name = 0;
  release(&irq_lock);
}

// Dispatch to dynamic IRQ handler if registered
// Returns 1 if handled, 0 if not
static int
irq_dispatch(int irq)
{
  if(irq < 0 || irq >= MAX_IRQ)
    return 0;
  
  irq_handler_t handler = irq_handlers[irq].handler;
  void *arg = irq_handlers[irq].arg;
  
  if(handler){
    handler(irq, arg);
    return 1;
  }
  return 0;
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
      exit();
    myproc()->tf = tf;
    syscall();
    proc_apply_pending_signals(myproc());
    proc_deliver_signal(myproc());
    proc_maybe_stop_current();
    if(myproc()->killed)
      exit();
    return;
  }

  switch(tf->trapno){
  case T_IRQ0 + IRQ_TIMER:
    if(cpuid() == 0){
      acquire(&tickslock);
      ticks++;
      wakeup(&ticks);
      release(&tickslock);
      // Check all processes for expired alarms
      proc_check_alarms(ticks);
      // TCP slow timer - every 10 ticks (100ms)
      if((ticks % 10) == 0)
        tcp_slowtimo();
    }
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
    proc_apply_pending_signals(myproc());
  if(myproc() && (tf->cs&3) == DPL_USER)
    proc_deliver_signal(myproc());
  if(myproc() && (tf->cs&3) == DPL_USER)
    proc_maybe_stop_current();

  // Force process exit if it has been killed and is in user space.
  // (If it is still executing in the kernel, let it keep running
  // until it gets to the regular system call return.)
  if(myproc() && myproc()->killed && (tf->cs&3) == DPL_USER)
    exit();

  // Force process to give up CPU on clock tick.
  // If interrupts were on while locks held, would need to check nlock.
  if(myproc() && myproc()->state == RUNNING &&
     tf->trapno == T_IRQ0+IRQ_TIMER)
    yield();

  // Check if the process has been killed since we yielded
  if(myproc() && myproc()->killed && (tf->cs&3) == DPL_USER)
    exit();
}
