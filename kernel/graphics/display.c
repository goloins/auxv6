/*
 * Display Device Abstraction for auxv6
 *
 * Kernel-side display mode setting and resource management.
 */

#include "types.h"
#include "defs.h"
#include "spinlock.h"
#include "graphics/display.h"
#include "graphics/framebuffer.h"

#define DISPLAY_MAX_DEVICES 4

/* Display device tracking */
static struct {
    struct spinlock lock;
    struct display_device *devices[DISPLAY_MAX_DEVICES];
    int count;
} display_registry;

void
display_init(void)
{
    initlock(&display_registry.lock, "display");
    display_registry.count = 0;
    memset(display_registry.devices, 0, sizeof(display_registry.devices));
}

/*
 * Allocate a display device structure
 */
struct display_device *
display_device_alloc(void)
{
    struct display_device *dev;

    dev = (struct display_device *)kalloc();
    if(!dev)
        return 0;

    memset(dev, 0, sizeof(*dev));
    dev->ref_count = 1;
    initlock(&dev->lock, "display_dev");

    return dev;
}

/*
 * Free a display device
 */
void
display_device_free(struct display_device *dev)
{
    if(!dev)
        return;

    if(dev->connectors)
        kfree((void *)dev->connectors);
    if(dev->encoders)
        kfree((void *)dev->encoders);
    if(dev->crtcs)
        kfree((void *)dev->crtcs);
    if(dev->framebuffers)
        kfree((void *)dev->framebuffers);

    kfree((void *)dev);
}

/*
 * Register a display device
 */
int
display_device_register(struct display_device *dev)
{
    if(!dev)
        return -1;

    acquire(&display_registry.lock);

    if(display_registry.count >= DISPLAY_MAX_DEVICES) {
        release(&display_registry.lock);
        return -1;
    }

    dev->device_id = display_registry.count;
    display_registry.devices[display_registry.count] = dev;
    display_registry.count++;

    release(&display_registry.lock);

    BOOTDBG("display: registered device %d (%s)\n", dev->device_id, dev->name);

    /* Probe connectors if driver provides callback */
    if(dev->ops && dev->ops->probe) {
        if(dev->ops->probe(dev) < 0) {
            cprintf("display: probe failed for device %d\n", dev->device_id);
        }
    }

    return dev->device_id;
}

/*
 * Unregister a display device
 */
int
display_device_unregister(struct display_device *dev)
{
    if(!dev)
        return -1;

    acquire(&display_registry.lock);

    for(int i = 0; i < display_registry.count; i++) {
        if(display_registry.devices[i] == dev) {
            if(dev->ops && dev->ops->remove) {
                dev->ops->remove(dev);
            }
            display_registry.devices[i] = display_registry.devices[display_registry.count - 1];
            display_registry.count--;
            release(&display_registry.lock);
            return 0;
        }
    }

    release(&display_registry.lock);
    return -1;
}

/*
 * Get a display device by ID
 */
struct display_device *
display_device_get(uint id)
{
    struct display_device *dev = 0;

    acquire(&display_registry.lock);

    if(id < (uint)display_registry.count) {
        dev = display_registry.devices[id];
    }

    release(&display_registry.lock);

    return dev;
}

/*
 * Increment device reference count
 */
void
display_device_ref(struct display_device *dev)
{
    if(!dev)
        return;

    acquire(&dev->lock);
    dev->ref_count++;
    release(&dev->lock);
}

/*
 * Decrement device reference count and free if needed
 */
void
display_device_unref(struct display_device *dev)
{
    if(!dev)
        return;

    acquire(&dev->lock);
    dev->ref_count--;
    if(dev->ref_count <= 0) {
        release(&dev->lock);
        display_device_free(dev);
        return;
    }
    release(&dev->lock);
}

/*
 * Probe all display devices
 */
int
display_probe_all(void)
{
    int count = 0;

    /* TODO: Call PCI bus scan for graphics class devices (0x03)
     *       Match against known device IDs and call probe handlers
     */

    cprintf("display: probed %d devices\n", count);
    return count;
}

/*
 * Get the primary display device
 */
struct display_device *
display_get_primary(void)
{
    struct display_device *dev = 0;

    acquire(&display_registry.lock);

    for(int i = 0; i < display_registry.count; i++) {
        if(display_registry.devices[i]->is_primary) {
            dev = display_registry.devices[i];
            break;
        }
    }

    /* Fallback to first device if no primary set */
    if(!dev && display_registry.count > 0) {
        dev = display_registry.devices[0];
    }

    release(&display_registry.lock);

    return dev;
}

/*
 * Get number of display devices
 */
int
display_num_devices(void)
{
    acquire(&display_registry.lock);
    int count = display_registry.count;
    release(&display_registry.lock);
    return count;
}

/*
 * Get a display device by index
 */
struct display_device *
display_get_device(int index)
{
    struct display_device *dev = 0;

    acquire(&display_registry.lock);

    if(index >= 0 && index < display_registry.count) {
        dev = display_registry.devices[index];
    }

    release(&display_registry.lock);

    return dev;
}

/* === Mode management === */

int
display_get_preferred_mode(struct display_device *dev,
                          struct display_connector *conn,
                          struct display_mode *out)
{
    if(!dev || !conn || !out)
        return -1;

    if(!dev->ops || !dev->ops->get_modes)
        return -1;

    if(conn->preferred_mode < conn->num_modes) {
        *out = conn->modes[conn->preferred_mode];
        return 0;
    }

    return -1;
}

int
display_set_mode(struct display_device *dev, struct display_crtc *crtc,
                 struct display_mode *mode)
{
    if(!dev || !crtc || !mode)
        return -1;

    if(!dev->ops || !dev->ops->set_mode)
        return -1;

    return dev->ops->set_mode(dev, crtc, mode);
}

/* === Framebuffer management === */

struct framebuffer *
display_create_framebuffer(struct display_device *dev,
                          uint width, uint height, uint pixfmt)
{
    if(!dev)
        return 0;

    if(dev->ops && dev->ops->create_framebuffer) {
        return dev->ops->create_framebuffer(dev, width, height, pixfmt);
    }

    /* Fallback: allocate generic framebuffer */
    return fb_alloc(width, height, pixfmt);
}

int
display_set_scanout(struct display_device *dev, struct display_crtc *crtc,
                   struct framebuffer *fb)
{
    if(!dev || !crtc || !fb)
        return -1;

    if(!dev->ops || !dev->ops->set_scanout)
        return -1;

    return dev->ops->set_scanout(dev, crtc, fb);
}

int
display_pageflip(struct display_device *dev, struct display_crtc *crtc,
                struct framebuffer *fb)
{
    if(!dev || !crtc || !fb)
        return -1;

    if(dev->ops && dev->ops->pageflip) {
        return dev->ops->pageflip(dev, crtc, fb);
    }

    return display_set_scanout(dev, crtc, fb);
}

/* === Rendering operations === */

int
display_flush(struct display_device *dev, struct framebuffer *fb)
{
    struct dirty_rect rect;

    if(!dev || !fb)
        return -1;

    if(dev->ops && dev->ops->flush_region && fb_is_dirty(fb)) {
        fb_get_dirty_rect(fb, &rect);
        if(rect.right >= rect.left && rect.bottom >= rect.top) {
            if(dev->ops->flush_region(dev, fb, &rect) < 0)
                return -1;
            fb_clear_dirty(fb);
            return 0;
        }
    }

    if(dev->ops && dev->ops->flush) {
        return dev->ops->flush(dev, fb);
    }

    return 0;
}

int
display_flush_region(struct display_device *dev, struct framebuffer *fb,
                    int x, int y, uint w, uint h)
{
    struct dirty_rect rect;

    if(!dev || !fb)
        return -1;

    rect.left = x;
    rect.top = y;
    rect.right = x + w - 1;
    rect.bottom = y + h - 1;

    if(dev->ops && dev->ops->flush_region) {
        return dev->ops->flush_region(dev, fb, &rect);
    }

    return display_flush(dev, fb);
}

/* === Synchronization === */

int
display_wait_vsync(struct display_device *dev, struct display_crtc *crtc)
{
    if(!dev || !crtc)
        return -1;

    if(dev->ops && dev->ops->wait_vsync) {
        return dev->ops->wait_vsync(dev, crtc);
    }

    return 0;
}

/* === Queries === */

int
display_connector_status(struct display_device *dev,
                        struct display_connector *conn)
{
    if(!dev || !conn)
        return CONN_STATUS_UNKNOWN;

    if(dev->ops && dev->ops->probe_connector) {
        dev->ops->probe_connector(dev, conn);
    }

    return conn->status;
}

int
display_get_connector_info(struct display_device *dev, uint conn_id,
                          struct display_connector *out)
{
    if(!dev || !out)
        return -1;

    if(conn_id >= dev->num_connectors)
        return -1;

    *out = dev->connectors[conn_id];
    return 0;
}

/* === Utilities === */

const char *
display_connector_type_str(int type)
{
    switch(type) {
    case CONN_VGA:      return "VGA";
    case CONN_HDMI:     return "HDMI";
    case CONN_DP:       return "DisplayPort";
    case CONN_INTERNAL: return "Internal";
    case CONN_USB:      return "USB";
    case CONN_VIRTUAL:  return "Virtual";
    default:            return "Unknown";
    }
}

const char *
display_mode_str(struct display_mode *mode, char *buf, int len)
{
    if(!mode || !buf || len <= 0)
        return "";

    /* Simple formatting without snprintf - not fully featured but works */
    if(len > 0) {
        buf[0] = 0;
        /* TODO: Implement lightweight mode string formatting */
    }
    return buf;
}

/* === Master access control === */

int
display_set_master(struct display_device *dev)
{
    if(!dev)
        return -1;

    /* TODO: Implement master access control */
    return 0;
}

int
display_drop_master(struct display_device *dev)
{
    if(!dev)
        return -1;

    /* TODO: Implement master access control */
    return 0;
}
