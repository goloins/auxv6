#include "types.h"
#include "defs.h"
#include "param.h"
#include "memlayout.h"
#include "mmu.h"
#include "proc.h"
#include "x86.h"

static void startothers(void);
static void mpmain(void)  __attribute__((noreturn));
extern pde_t *kpgdir;
extern char end[]; // first address after kernel loaded from ELF file

// Bootstrap processor starts running C code here.
// Allocate a real stack and switch to it, first
// doing some setup required for memory allocator to work.
int
main(void)
{
  kinit1(end, P2V(BOOT_EARLY_PHYSTOP)); // phys page allocator (early mapped window)
  kvmalloc();      // kernel page table
  mpinit();        // detect other processors
  lapicinit();     // interrupt controller
  seginit();       // segment descriptors
  picinit();       // disable pic
  ioapicinit();    // another interrupt controller
  display_init();  // display device registry
  pci_init();      // PCI bus enumeration
  modem_init();    // modem-class PCI probe stubs
  firewire_init(); // firewire/IEEE1394 PCI probe stubs
  wifi_init();     // 802.11 Wi-Fi PCI probe scaffold
  ieee802154_init(); // 802.15.4 WPAN scaffold (backburner; USB attach pending)
  usb_init();      // usb host-controller discovery scaffold
  virtio_gpu_init(); // virtio-gpu subsystem
  intel_gfx_init();  // intel display-class PCI probe stub
  consoleinit();   // console hardware
  ptyinit();       // pseudo-terminal endpoints
  serialinit();    // serial tty chardev endpoints
  audio_init();    // audio char-device skeleton and ioctl entrypoints
  tuntap_init();   // tun/tap char-device scaffold (/dev/net/tun)
  uartinit();      // serial port
  pinit();         // process table
  tvinit();        // trap vectors
  bdevinit();      // block device switch
  binit();         // buffer cache
  kmalloc_init();  // Phase 1A: kernel malloc allocator
  fileinit();      // file table
  ideinit();       // disk
  ahci_init();     // AHCI SATA controllers
  nvme_init();     // NVMe controllers
  virtio_blk_init(); // virtio block devices
  loop_init();     // loop block devices
  netdev_init();   // network interfaces
  socket_init();   // socket table
  startothers();   // start other processors
  kinit2(P2V(BOOT_EARLY_PHYSTOP), P2V(PHYSTOP)); // must come after startothers()
  console_gfx_late_enable(); // framebuffer mirror can allocate after full memory is online
  lockdep_enable(); // enable lockdep after early bring-up to avoid pre-console hard-fail
  userinit();      // first user process
  mpmain();        // finish this processor's setup
}

// Other CPUs jump here from entryother.S.
static void
mpenter(void)
{
  switchkvm();
  seginit();
  lapicinit();
  mpmain();
}

// Common CPU setup code.
static void
mpmain(void)
{
  BOOTDBG("cpu%d: starting %d\n", cpuid(), cpuid());
  idtinit();       // load idt register
  xchg(&(mycpu()->started), 1); // tell startothers() we're up
  scheduler();     // start running processes
}

pde_t entrypgdir[];  // For entry.S

// Start the non-boot (AP) processors.
static void
startothers(void)
{
  extern uchar _binary_entryother_start[], _binary_entryother_size[];
  uchar *code;
  struct cpu *c;
  char *stack;

  // Write entry code to unused memory at 0x7000.
  // The linker has placed the image of entryother.S in
  // _binary_entryother_start.
  code = P2V(0x7000);
  memmove(code, _binary_entryother_start, (uint)_binary_entryother_size);

  for(c = cpus; c < cpus+ncpu; c++){
    if(c == mycpu())  // We've started already.
      continue;

    // Tell entryother.S what stack to use, where to enter, and what
    // pgdir to use. We cannot use kpgdir yet, because the AP processor
    // is running in low  memory, so we use entrypgdir for the APs too.
    stack = kalloc();
    *(void**)(code-4) = stack + KSTACKSIZE;
    *(void(**)(void))(code-8) = mpenter;
    *(int**)(code-12) = (void *) V2P(entrypgdir);

    lapicstartap(c->apicid, V2P(code));

    // wait for cpu to finish mpmain()
    while(c->started == 0)
      ;
  }
}

// The boot page table used in entry.S and entryother.S.
// Page directories (and page tables) must start on page boundaries,
// hence the __aligned__ attribute.
// PTE_PS in a page directory entry enables 4Mbyte pages.
// Keep this in sync with BOOT_EARLY_PHYSTOP in memlayout.h.

#if (BOOT_EARLY_PHYSTOP != (16*1024*1024))
#error "entrypgdir initializer assumes BOOT_EARLY_PHYSTOP == 16MB"
#endif

__attribute__((__aligned__(PGSIZE)))
pde_t entrypgdir[NPDENTRIES] = {
  // Map VA's [0, 16MB) to PA's [0, 16MB)
  [0] = (0) | PTE_P | PTE_W | PTE_PS,
  [1] = (0x00400000) | PTE_P | PTE_W | PTE_PS,
  [2] = (0x00800000) | PTE_P | PTE_W | PTE_PS,
  [3] = (0x00C00000) | PTE_P | PTE_W | PTE_PS,
  // Map VA's [KERNBASE, KERNBASE+16MB) to PA's [0, 16MB)
  [KERNBASE>>PDXSHIFT] = (0) | PTE_P | PTE_W | PTE_PS,
  [(KERNBASE>>PDXSHIFT) + 1] = (0x00400000) | PTE_P | PTE_W | PTE_PS,
  [(KERNBASE>>PDXSHIFT) + 2] = (0x00800000) | PTE_P | PTE_W | PTE_PS,
  [(KERNBASE>>PDXSHIFT) + 3] = (0x00C00000) | PTE_P | PTE_W | PTE_PS,
};

//PAGEBREAK!
// Blank page.
//PAGEBREAK!
// Blank page.
//PAGEBREAK!
// Blank page.

