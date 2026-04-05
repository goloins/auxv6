/*
 * DMA Memory Allocation for auxv6
 *
 * Provides physically contiguous memory allocation for device DMA.
 * In xv6's simplified memory model, all kernel memory is identity-mapped
 * relative to KERNBASE, so V2P/P2V work for kalloc'd pages.
 *
 * For devices that need memory below specific physical addresses
 * (e.g., ISA DMA requires < 16MB), we'd need a zone allocator.
 * For now, this implementation works with modern PCI devices
 * that can DMA to any 32-bit address and can consume contiguous
 * multi-page allocations.
 *
 * TODO Future enhancements:
 * - [ ] Zone-based allocation (DMA32, normal, etc.)
 * - [ ] Large contiguous allocations (> 1 page)
 * - [ ] IOMMU support
 * - [ ] Bounce buffers for devices with addressing limitations
 */

#include "types.h"
#include "defs.h"
#include "memlayout.h"
#include "mmu.h"
#include "spinlock.h"

static void
dma_note(const char *msg)
{
    const char *p;

    if(!msg)
        return;
    for(p = msg; *p; p++)
        uartputc(*p);
}

/* DMA allocation tracking */
#define MAX_DMA_ALLOCS 64

struct dma_region {
    void    *vaddr;      /* kernel virtual address */
    uint paddr;      /* physical address */
    uint size;       /* allocation size */
    int      in_use;
};

static struct {
    struct spinlock lock;
    struct dma_region regions[MAX_DMA_ALLOCS];
    int initialized;
} dma_state;

static void
dma_free_region_pages(char *base, uint size)
{
    uint pages;
    uint i;

    if(!base || size == 0)
        return;

    pages = PGROUNDUP(size) / PGSIZE;
    for(i = 0; i < pages; i++)
        kfree(base + i * PGSIZE);
}

static void *
dma_alloc_contiguous_pages(uint size, uint *phys_out)
{
    char *base;
    uint pages;

    pages = PGROUNDUP(size) / PGSIZE;
    if(pages == 0)
        return 0;

    base = kalloc_contiguous(pages);
    if(!base)
        return 0;
    if(phys_out)
        *phys_out = V2P(base);
    return base;
}

void
dma_init(void)
{
    initlock(&dma_state.lock, "dma");
    for(int i = 0; i < MAX_DMA_ALLOCS; i++){
        dma_state.regions[i].vaddr = 0;
        dma_state.regions[i].paddr = 0;
        dma_state.regions[i].size = 0;
        dma_state.regions[i].in_use = 0;
    }
    dma_state.initialized = 1;
}

/*
 * Allocate physically contiguous memory for DMA
 * 
 * size: bytes to allocate (rounded up to page size)
 * phys_out: if non-NULL, receives the physical address
 *
 * Returns: kernel virtual address, or NULL on failure
 *
 * Note: Multi-page allocations require a contiguous run of free pages.
 * That is sufficient for the current early-boot framebuffer path,
 * but this is still not a full general-purpose DMA arena.
 */
void *
dma_alloc(uint size, uint *phys_out)
{
    void *vaddr;
    int slot = -1;
    
    if(!dma_state.initialized)
        dma_init();
    
    /* Round up to page size */
    size = PGROUNDUP(size);

    vaddr = dma_alloc_contiguous_pages(size, phys_out);
    if(!vaddr)
        return 0;
    
    /* Track the allocation */
    acquire(&dma_state.lock);
    for(int i = 0; i < MAX_DMA_ALLOCS; i++){
        if(!dma_state.regions[i].in_use){
            slot = i;
            break;
        }
    }
    if(slot >= 0){
        dma_state.regions[slot].vaddr = vaddr;
        dma_state.regions[slot].paddr = V2P(vaddr);
        dma_state.regions[slot].size = size;
        dma_state.regions[slot].in_use = 1;
    }
    release(&dma_state.lock);
    
    if(slot < 0){
        dma_note("dma_alloc: too many allocations\n");
        dma_free_region_pages((char *)vaddr, size);
        return 0;
    }

    return vaddr;
}

/*
 * Free DMA memory
 */
void
dma_free(void *vaddr, uint size)
{
    uint tracked_size;

    if(!vaddr)
        return;

    tracked_size = PGROUNDUP(size);
    
    acquire(&dma_state.lock);
    for(int i = 0; i < MAX_DMA_ALLOCS; i++){
        if(dma_state.regions[i].in_use && 
           dma_state.regions[i].vaddr == vaddr){
            tracked_size = dma_state.regions[i].size;
            dma_state.regions[i].in_use = 0;
            dma_state.regions[i].vaddr = 0;
            dma_state.regions[i].paddr = 0;
            dma_state.regions[i].size = 0;
            break;
        }
    }
    release(&dma_state.lock);

    dma_free_region_pages((char *)vaddr, tracked_size);
}

/*
 * Get physical address of previously allocated DMA memory
 */
uint
dma_virt_to_phys(void *vaddr)
{
    if(!vaddr)
        return 0;
    
    acquire(&dma_state.lock);
    for(int i = 0; i < MAX_DMA_ALLOCS; i++){
        if(dma_state.regions[i].in_use &&
           dma_state.regions[i].vaddr == vaddr){
            uint paddr = dma_state.regions[i].paddr;
            release(&dma_state.lock);
            return paddr;
        }
    }
    release(&dma_state.lock);
    
    /* Fall back to V2P for kernel addresses */
    return V2P(vaddr);
}

/*
 * Allocate a DMA-capable buffer with guaranteed alignment
 */
void *
dma_alloc_aligned(uint size, uint align, uint *phys_out)
{
    /* kalloc already returns page-aligned memory */
    if(align <= PGSIZE)
        return dma_alloc(size, phys_out);
    
    /* Larger alignments not yet supported */
    dma_note("dma_alloc_aligned: alignment > PGSIZE not supported\n");
    return 0;
}

/*
 * Sync memory for device access (cache operations)
 * On x86 without write-back caching complications, this is a no-op
 * for most cases, but we include it for API completeness.
 */
void
dma_sync_for_device(void *vaddr, uint size)
{
    /* x86 with write-through or MTRR-controlled regions doesn't need this */
    /* For completeness, could use CLFLUSH instructions here */
    (void)vaddr;
    (void)size;
}

void
dma_sync_for_cpu(void *vaddr, uint size)
{
    /* Same as above - x86 cache coherent */
    (void)vaddr;
    (void)size;
}
