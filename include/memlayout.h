// Memory layout

#define EXTMEM  0x100000            // Start of extended memory
#define PHYSTOP 0x20000000          // Top physical memory (512 MB)
#define DEVSPACE 0xFE000000         // Other devices are at high addresses

// Early-boot identity/high mapping window used by entrypgdir before kvmalloc.
// Must be a multiple of 4MB because entrypgdir uses 4MB PDEs (PTE_PS).
#define BOOT_EARLY_PHYSTOP 0x00800000  // 8 MB

// Key addresses for address space layout (see kmap in vm.c for layout)
#define KERNBASE 0x80000000         // First kernel virtual address
#define KERNLINK (KERNBASE+EXTMEM)  // Address where kernel is linked

#define V2P(a) (((uint) (a)) - KERNBASE)
#define P2V(a) ((void *)(((char *) (a)) + KERNBASE))

#define V2P_WO(x) ((x) - KERNBASE)    // same as V2P, but without casts
#define P2V_WO(x) ((x) + KERNBASE)    // same as P2V, but without casts
