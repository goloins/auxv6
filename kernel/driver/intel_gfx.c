/*
 * Intel graphics PCI probe stub for auxv6.
 *
 * Scope:
 * - Discover Intel display-class devices on PCI.
 * - Enable PCI memory and bus mastering.
 * - Map one MMIO BAR following i915-style priority (BAR0, then BAR2).
 *
 * Non-goals for this tranche:
 * - No mode setting, ring submission, GEM/TTM, or IRQ-driven rendering.
 * - No ownership changes in the active framebuffer or console path.
 *
 * Reference direction:
 * - Linux: drivers/gpu/drm/i915 (BAR usage and early attach shape)
 * - BSD DRM ports (attach first, modeset/render follow in later phases)
 */

#include "types.h"
#include "defs.h"
#include "spinlock.h"
#include "pci.h"

#define INTEL_GFX_MAX_DEVS 4
#define INTEL_GFX_SUBCLASS_VGA 0x00
#define INTEL_GFX_SUBCLASS_XGA 0x01
#define INTEL_GFX_SUBCLASS_3D  0x02
#define INTEL_GFX_SUBCLASS_OTHER 0x80

struct intel_gfx_softc {
    struct pci_dev *pci;
    struct spinlock lock;
    volatile uint32_t *regs;
    uint32_t mmio_base;
    uint32_t mmio_size;
    int mmio_bar;
};

static struct intel_gfx_softc intel_gfx_devs[INTEL_GFX_MAX_DEVS];
static int intel_gfx_count;

static int
intel_gfx_match(struct pci_dev *dev)
{
    if(!dev)
        return 0;
    if(dev->vendor_id != PCI_VENDOR_INTEL)
        return 0;
    if(dev->class_code != PCI_CLASS_DISPLAY)
        return 0;

    switch(dev->subclass) {
    case INTEL_GFX_SUBCLASS_VGA:
    case INTEL_GFX_SUBCLASS_XGA:
    case INTEL_GFX_SUBCLASS_3D:
    case INTEL_GFX_SUBCLASS_OTHER:
        return 1;
    default:
        return 0;
    }
}

static int
intel_gfx_pick_mmio_bar(struct pci_dev *dev)
{
    int bar;

    if(!dev)
        return -1;

    /* Linux i915 convention is BAR0 for MMIO on most generations. */
    if((dev->bar_type[0] & PCI_BAR_IO) == 0 && pci_bar_size(dev, 0) >= 4096)
        return 0;

    /* Older layouts can expose useful aperture metadata around BAR2. */
    if((dev->bar_type[2] & PCI_BAR_IO) == 0 && pci_bar_size(dev, 2) >= 4096)
        return 2;

    for(bar = 0; bar < 6; bar++) {
        if((dev->bar_type[bar] & PCI_BAR_IO) != 0)
            continue;
        if(pci_bar_size(dev, bar) < 4096)
            continue;
        return bar;
    }

    return -1;
}

static void
intel_gfx_attach(struct pci_dev *dev)
{
    struct intel_gfx_softc *sc;
    int bar;

    if(!dev)
        return;

    if(intel_gfx_count >= INTEL_GFX_MAX_DEVS) {
        cprintf("intel_gfx: max devices reached, skipping %x\n", dev->device_id);
        return;
    }

    sc = &intel_gfx_devs[intel_gfx_count];
    memset(sc, 0, sizeof(*sc));
    sc->pci = dev;
    initlock(&sc->lock, "intel_gfx");

    pci_enable_mem(dev);
    pci_set_master(dev);

    bar = intel_gfx_pick_mmio_bar(dev);
    sc->mmio_bar = bar;
    if(bar >= 0) {
        sc->mmio_base = pci_bar_base(dev, bar);
        sc->mmio_size = pci_bar_size(dev, bar);
        sc->regs = (volatile uint32_t *)pci_map_bar(dev, bar);
    }

    if(bar < 0 || !sc->regs) {
        cprintf("intel_gfx: attached %d:%d.%d dev=%x without mapped MMIO (bar=%d)\n",
                dev->bus, dev->slot, dev->func, dev->device_id, bar);
    } else {
        cprintf("intel_gfx: attached %d:%d.%d dev=%x irq=%d mmio=bar%d base=%x size=%x\n",
                dev->bus, dev->slot, dev->func, dev->device_id, dev->irq_line,
                bar, sc->mmio_base, sc->mmio_size);
    }

    intel_gfx_count++;
}

void
intel_gfx_init(void)
{
    int i;
    int found;

    intel_gfx_count = 0;
    found = 0;

    BOOTDBG("intel_gfx: probing intel display-class devices\n");

    for(i = 0; i < pci_device_count(); i++) {
        struct pci_dev *dev = pci_get_device(i);
        if(!intel_gfx_match(dev))
            continue;
        found = 1;
        intel_gfx_attach(dev);
    }

    if(!found)
        BOOTDBG("intel_gfx: no intel display-class devices\n");
}
