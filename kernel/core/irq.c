#include "types.h"
#include "defs.h"
#include "x86.h"
#include "traps.h"
#include "spinlock.h"

// Dynamic IRQ handler table
// Indexed by IRQ number (0-15 for legacy PIC, 0-23 for IOAPIC)
// Supports shared interrupts with a chain of handlers
#define MAX_IRQ (256 - T_IRQ0)
#define MAX_HANDLERS_PER_IRQ 8

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
void
irq_init(void)
{
  initlock(&irq_lock, "irq");
  lockdep_set_rank(&irq_lock, LOCK_RANK_DEFAULT, "irq");
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
int
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