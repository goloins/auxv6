#include "types.h"
#include "defs.h"
#include "param.h"
#include "memlayout.h"
#include "mmu.h"
#include "proc.h"
#include "x86.h"

static volatile uint trap_fatal_latched;
extern volatile uint *lapic;

static void
trap_uart_puts(const char *s)
{
  if(!s)
    return;
  while(*s)
    uartputc(*s++);
}

static void
trap_uart_put_hex(uint x)
{
  static char digits[] = "0123456789abcdef";
  int i;

  trap_uart_puts("0x");
  for(i = 7; i >= 0; i--)
    uartputc(digits[(x >> (i * 4)) & 0xF]);
}

static void
trap_emergency_report(struct trapframe *tf, const char *reason)
{
  uchar apicid;
  struct proc *p;
  uint kbase;
  uint ktop;
  uint ebp;
  uint ret;

  apicid = cpu_apicid_cpuid();
  trap_uart_puts("\nFATAL trap: ");
  trap_uart_puts(reason ? reason : "unknown");
  trap_uart_puts(" apic=");
  trap_uart_put_hex((uint)apicid);
  trap_uart_puts(" trap=");
  trap_uart_put_hex((uint)tf->trapno);
  trap_uart_puts(" err=");
  trap_uart_put_hex((uint)tf->err);
  trap_uart_puts(" eip=");
  trap_uart_put_hex((uint)tf->eip);
  trap_uart_puts(" cs=");
  trap_uart_put_hex((uint)tf->cs);
  trap_uart_puts(" esp=");
  trap_uart_put_hex((uint)tf->esp);
  trap_uart_puts(" ebp=");
  trap_uart_put_hex((uint)tf->ebp);
  trap_uart_puts(" cr3=");
  trap_uart_put_hex(rcr3());
  trap_uart_puts(" lapic=");
  trap_uart_put_hex((uint)lapic);
  trap_uart_puts(" cr2=");
  trap_uart_put_hex(rcr2());

  p = myproc();
  if(p && p->kstack){
    kbase = (uint)p->kstack;
    ktop = kbase + KSTACKSIZE;
    trap_uart_puts(" kstack=");
    trap_uart_put_hex(kbase);
    trap_uart_puts("..");
    trap_uart_put_hex(ktop);

    /* When faulting inside memmove, ebp+4 is the return/caller address. */
    if((uint)tf->eip >= (uint)memmove && (uint)tf->eip < (uint)memmove + 0x100){
      ebp = (uint)tf->ebp;
      if(ebp >= kbase && ebp + 8 <= ktop){
        ret = *(uint*)(ebp + 4);
        trap_uart_puts(" memmove_ret=");
        trap_uart_put_hex(ret);
      }
    }
  }
  trap_uart_puts("\n");
}

void
trap_kernel_fatal(struct trapframe *tf, const char *reason)
{
  cli();
  if(xchg(&trap_fatal_latched, 1) == 0)
    trap_emergency_report(tf, reason);
  for(;;)
    asm volatile("hlt");
}