/*
 * VirtIO GPU Driver for auxv6
 *
 * Implements the VirtIO GPU device specification for paravirtualized graphics.
 *
 * Architecture:
 * - PCI discovery and initialization
 * - Two virtqueues: control (commands) and cursor
 * - Commands for resource creation, mode setting, and scanout
 *
 * TODO Phase 1:
 * - [x] PCI device detection
 * - [x] Virtqueue setup
 * - [x] Feature negotiation
 * - [x] GET_DISPLAY_INFO command and scanout discovery
 * - [x] Resource creation
 * - [x] Simple scanout
 * - [ ] Full mode-setting lifecycle beyond initial scanout discovery
 *
 * TODO Phase 2:
 * - [ ] Async command handling and responses
 * - [ ] Dirty rectangle transfers
 * - [ ] Multi-monitor support
 * - [ ] Virgl 3D rendering
 */

#include "types.h"
#include "defs.h"
#include "spinlock.h"
#include "pci.h"
#include "virtio.h"
#include "virtio_gpu.h"
#include "graphics/display.h"
#include "graphics/framebuffer.h"

#define VIRTIO_GPU_MAX_INSTANCES 4

struct virtio_gpu_connector_bundle {
    struct display_connector connector;
    struct display_mode mode;
};

static struct virtio_gpu_softc virtio_gpu_instances[VIRTIO_GPU_MAX_INSTANCES];
static int virtio_gpu_count = 0;
static struct spinlock virtio_gpu_lock;

static void
virtio_gpu_uartlog(const char *msg)
{
    const char *p;

    if(!msg)
        return;
    for(p = msg; *p; p++)
        uartputc(*p);
}

/* Forward declarations */
static int virtio_gpu_display_probe(struct display_device *dev);
static void virtio_gpu_irq_handler(int irq, void *arg);
static void virtio_gpu_intr(struct virtio_dev *vdev);
static int virtio_gpu_display_set_scanout(struct display_device *dev,
                                          struct display_crtc *crtc,
                                          struct framebuffer *fb);
static struct framebuffer *virtio_gpu_display_create_framebuffer(struct display_device *dev,
                                                                 uint width, uint height,
                                                                 uint pixfmt);
static void virtio_gpu_display_destroy_framebuffer(struct display_device *dev,
                                                   struct framebuffer *fb);
static int virtio_gpu_display_flush_region(struct display_device *dev,
                                           struct framebuffer *fb,
                                           struct dirty_rect *region);
static int virtio_gpu_display_flush(struct display_device *dev,
                                    struct framebuffer *fb);
static int virtio_gpu_refresh_display_info(struct virtio_gpu_softc *sc);
static int virtio_gpu_pick_scanout(struct virtio_gpu_softc *sc, int require_enabled);

static const struct display_device_ops virtio_gpu_display_ops = {
    .probe = virtio_gpu_display_probe,
    .create_framebuffer = virtio_gpu_display_create_framebuffer,
    .destroy_framebuffer = virtio_gpu_display_destroy_framebuffer,
    .set_scanout = virtio_gpu_display_set_scanout,
    .flush_region = virtio_gpu_display_flush_region,
    .flush = virtio_gpu_display_flush,
};

static int
virtio_gpu_pick_scanout(struct virtio_gpu_softc *sc, int require_enabled)
{
    int i;

    if(!sc)
        return -1;

    for(i = 0; i < VIRTIO_GPU_MAX_SCANOUTS; i++) {
        if(sc->scanouts[i].width == 0 || sc->scanouts[i].height == 0)
            continue;
        if(require_enabled && !sc->scanouts[i].enabled)
            continue;
        return i;
    }

    return -1;
}

static int
virtio_gpu_refresh_display_info(struct virtio_gpu_softc *sc)
{
    struct virtio_gpu_resp_display_info resp;
    int i;
    int count;

    if(!sc)
        return -1;

    memset(&resp, 0, sizeof(resp));
    if(virtio_gpu_cmd_get_display_info(sc, &resp) < 0)
        return -1;
    if(resp.hdr.type != VIRTIO_GPU_RESP_OK_DISPLAY_INFO)
        return -1;

    acquire(&sc->lock);
    memset(sc->scanouts, 0, sizeof(sc->scanouts));
    count = 0;
    for(i = 0; i < VIRTIO_GPU_MAX_SCANOUTS; i++) {
        sc->scanouts[i].enabled = resp.pmodes[i].enabled ? 1 : 0;
        sc->scanouts[i].width = resp.pmodes[i].r.width;
        sc->scanouts[i].height = resp.pmodes[i].r.height;
        if(sc->scanouts[i].width != 0 && sc->scanouts[i].height != 0)
            count++;
    }
    sc->num_scanouts = (uint)count;
    release(&sc->lock);

    return count > 0 ? 0 : -1;
}

static int
virtio_gpu_display_probe(struct display_device *dev)
{
    struct virtio_gpu_softc *sc;
    struct virtio_gpu_connector_bundle *bundle;
    int scanout_id;
    uint width;
    uint height;

    if(!dev)
        return -1;

    sc = (struct virtio_gpu_softc *)dev->driver_data;
    if(!sc)
        return -1;

    if(virtio_gpu_refresh_display_info(sc) < 0) {
        cprintf("virtio_gpu: GET_DISPLAY_INFO failed\n");
        return -1;
    }

    scanout_id = virtio_gpu_pick_scanout(sc, 1);
    if(scanout_id < 0)
        scanout_id = virtio_gpu_pick_scanout(sc, 0);
    if(scanout_id < 0) {
        cprintf("virtio_gpu: no usable scanout reported\n");
        return -1;
    }

    if(!dev->connectors) {
        bundle = (struct virtio_gpu_connector_bundle *)kalloc();
        if(!bundle)
            return -1;
        dev->connectors = &bundle->connector;
        dev->num_connectors = 1;
    }

    bundle = (struct virtio_gpu_connector_bundle *)dev->connectors;
    memset(bundle, 0, sizeof(*bundle));
    width = sc->scanouts[scanout_id].width;
    height = sc->scanouts[scanout_id].height;

    bundle->connector.connector_id = (uint)scanout_id;
    bundle->connector.type = CONN_VIRTUAL;
    bundle->connector.status = sc->scanouts[scanout_id].enabled ?
                               CONN_STATUS_CONNECTED : CONN_STATUS_UNKNOWN;
    bundle->connector.modes = &bundle->mode;
    bundle->connector.num_modes = 1;
    bundle->connector.preferred_mode = 0;
    memmove(bundle->connector.name, "Virtual-1", sizeof("Virtual-1"));

    bundle->mode.width = width;
    bundle->mode.height = height;
    bundle->mode.hdisplay = width;
    bundle->mode.vdisplay = height;
    bundle->mode.pixfmt = PIXFMT_XRGB8888;
    memmove(bundle->mode.name, "virtio-preferred", sizeof("virtio-preferred"));

    dev->current_mode = &bundle->mode;
    dev->max_width = width;
    dev->max_height = height;
    dev->min_width = width;
    dev->min_height = height;
    dev->max_framebuffers = VIRTIO_GPU_MAX_RESOURCES;

    if(dev->crtcs && dev->num_crtcs > 0) {
        dev->crtcs[0].connector_id = bundle->connector.connector_id;
        dev->crtcs[0].mode = &bundle->mode;
    }

    return 0;
}

/*
 * Probe for VirtIO GPU device
 *
 * Called when PCI scan detects a VirtIO device with device_id == VIRTIO_DEV_GPU
 */
int
virtio_gpu_probe(struct pci_dev *pci)
{
    struct virtio_gpu_softc *sc;
    struct virtio_dev *vdev;
    struct display_device *display_dev;

    if(!pci) {
        cprintf("virtio_gpu: pci_dev is null\n");
        return -1;
    }

    acquire(&virtio_gpu_lock);

    if(virtio_gpu_count >= VIRTIO_GPU_MAX_INSTANCES) {
        cprintf("virtio_gpu: too many instances\n");
        release(&virtio_gpu_lock);
        return -1;
    }

    sc = &virtio_gpu_instances[virtio_gpu_count];
    memset(sc, 0, sizeof(*sc));
    vdev = &sc->vdev;

    /* Probe virtio device */
    if(virtio_probe_pci(pci, vdev) < 0) {
        cprintf("virtio_gpu: failed to probe virtio device\n");
        release(&virtio_gpu_lock);
        return -1;
    }

    /* Verify it's a GPU device */
    if(vdev->device_id != VIRTIO_DEV_GPU) {
        cprintf("virtio_gpu: device_id mismatch (%d != %d)\n",
                vdev->device_id, VIRTIO_DEV_GPU);
        release(&virtio_gpu_lock);
        return -1;
    }

    /* Match the working virtio blk/net handshake sequence. */
    virtio_set_status(vdev, VIRTIO_STATUS_ACK | VIRTIO_STATUS_DRIVER);

    /* Initialize driver state */
    initlock(&sc->lock, "virtio_gpu");
    initlock(&sc->resp_lock, "virtio_gpu_resp");
    vdev->driver_data = sc;
    vdev->isr_handler = virtio_gpu_intr;
    sc->num_scanouts = 0;

    /* Feature negotiation */
    uint64_t features = 0;
    /* Request supported features */
    if(virtio_negotiate_features(vdev, features) < 0) {
        cprintf("virtio_gpu: feature negotiation failed\n");
        release(&virtio_gpu_lock);
        return -1;
    }

    /* Finalize features */
    if(virtio_finalize_features(vdev) < 0) {
        cprintf("virtio_gpu: feature finalization failed\n");
        release(&virtio_gpu_lock);
        return -1;
    }

    /* Create virtqueues */
    struct virtqueue *ctrl_q = virtq_create(vdev, VIRTIO_GPU_Q_CONTROL, VIRTQ_SIZE_DEFAULT);
    struct virtqueue *cursor_q = virtq_create(vdev, VIRTIO_GPU_Q_CURSOR, VIRTQ_SIZE_DEFAULT);

    if(!ctrl_q || !cursor_q) {
        cprintf("virtio_gpu: failed to create virtqueues\n");
        release(&virtio_gpu_lock);
        return -1;
    }

    /* Mark driver ready */
    virtio_set_status(vdev, VIRTIO_STATUS_DRIVER_OK);

    BOOTDBG("virtio_gpu: successfully initialized device at %d:%d.%d\n",
            pci->bus, pci->slot, pci->func);

    /* Register IRQ handler */
    if(vdev->irq > 0) {
        ioapicenable(vdev->irq, 0);
        irq_register(vdev->irq, virtio_gpu_irq_handler, sc, "virtio-gpu");
    }

    /* Create display device abstraction */
    display_dev = display_device_alloc();
    if(display_dev) {
        /* Simple device naming without snprintf */
        char *src = "virtio-gpu";
        char *dst = display_dev->name;
        while(*src && dst - display_dev->name < (int)sizeof(display_dev->name) - 2)
            *dst++ = *src++;
        *dst = 0;
        
        display_dev->is_primary = (virtio_gpu_count == 0);
        display_dev->max_width = 4096;
        display_dev->max_height = 4096;
        display_dev->min_width = 320;
        display_dev->min_height = 240;
        display_dev->ops = &virtio_gpu_display_ops;
        display_dev->num_crtcs = 1;
        display_dev->crtcs = (struct display_crtc *)kalloc();
        if(display_dev->crtcs) {
            memset(display_dev->crtcs, 0, sizeof(struct display_crtc));
            display_dev->crtcs[0].crtc_id = 0;
        } else {
            display_dev->num_crtcs = 0;
        }
        display_dev->driver_data = sc;
        sc->display_dev = display_dev;

        if(display_device_register(display_dev) < 0) {
            cprintf("virtio_gpu: failed to register display device\n");
            display_device_free(display_dev);
            sc->display_dev = 0;
        }
    }

    virtio_gpu_count++;

    release(&virtio_gpu_lock);

    cprintf("virtio_gpu: device initialized successfully\n");
    return 0;
}

/*
 * Remove a VirtIO GPU device
 */
void
virtio_gpu_remove(struct virtio_gpu_softc *sc)
{
    if(!sc)
        return;

    if(sc->display_dev) {
        display_device_unregister(sc->display_dev);
        sc->display_dev = 0;
    }

    memset(sc, 0, sizeof(*sc));
}

/*
 * IRQ handler wrapper
 */
static void
virtio_gpu_irq_handler(int irq, void *arg)
{
    struct virtio_gpu_softc *sc = (struct virtio_gpu_softc *)arg;

    (void)irq;

    if(sc)
        virtio_handle_interrupt(&sc->vdev);
}

/*
 * Interrupt handler - called from virtio_handle_interrupt
 */
static void
virtio_gpu_intr(struct virtio_dev *vdev)
{
    struct virtio_gpu_softc *sc = vdev->driver_data;

    if(!sc)
        return;

    /* TODO: Process completed commands from control queue */
    /* TODO: Process cursor updates */

    BOOTDBG("virtio_gpu: interrupt received\n");
}

/*
 * Submit a command and optionally wait for response
 *
 * Returns 0 on success, -1 on failure
 */
static int
virtio_gpu_cmd_submit(struct virtio_gpu_softc *sc,
                      struct virtio_gpu_ctrl_hdr *hdr,
                      uint req_size,
                      void *response, uint response_size,
                      uint timeout_ms)
{
    struct virtqueue *vq;
    void *bufs[2];
    uint32_t lens[2];
    int rc;
    int tries;
    int max_tries;
    uint32_t used_len;
    void *cookie;

    if(!sc || !hdr)
        return -1;

    vq = sc->vdev.vqs[VIRTIO_GPU_Q_CONTROL];
    if(!vq)
        return -1;

    acquire(&sc->lock);

    /* Setup descriptor chain: [request; response] */
    bufs[0] = hdr;
    bufs[1] = response;
    lens[0] = req_size;
    lens[1] = response_size;

    rc = virtq_add_buf(vq, bufs, lens, 1, 1, hdr);
    if(rc < 0) {
        release(&sc->lock);
        return -1;
    }

    virtq_kick(vq);
    max_tries = (int)(timeout_ms ? timeout_ms * 1000 : 100000);
    if(max_tries < 10000)
        max_tries = 10000;

    for(tries = 0; tries < max_tries; tries++) {
        cookie = virtq_get_buf(vq, &used_len);
        if(cookie == hdr) {
            release(&sc->lock);
            return 0;
        }
    }

    release(&sc->lock);
    cprintf("virtio_gpu: command timeout type=%x\n", hdr->type);
    return -1;
}

/* ============ Device commands ============ */

/*
 * GET_DISPLAY_INFO command - query available displays
 */
int
virtio_gpu_cmd_get_display_info(struct virtio_gpu_softc *sc,
                               struct virtio_gpu_resp_display_info *resp)
{
    struct virtio_gpu_get_display_info cmd;

    if(!sc || !resp)
        return -1;

    memset(&cmd, 0, sizeof(cmd));
    cmd.hdr.type = VIRTIO_GPU_CMD_GET_DISPLAY_INFO;
    cmd.hdr.flags = 0;
    cmd.hdr.fence_id = 0;
    cmd.hdr.ctx_id = 0;

    return virtio_gpu_cmd_submit(sc, &cmd.hdr, sizeof(cmd), resp, sizeof(*resp), 1000);
}

/*
 * RESOURCE_CREATE_2D command - allocate a 2D drawing surface
 */
int
virtio_gpu_cmd_resource_create_2d(struct virtio_gpu_softc *sc,
                                 uint32_t resource_id, uint32_t format,
                                 uint32_t width, uint32_t height)
{
    struct virtio_gpu_resource_create_2d cmd;
    struct virtio_gpu_ctrl_hdr resp;

    if(!sc)
        return -1;

    memset(&cmd, 0, sizeof(cmd));
    cmd.hdr.type = VIRTIO_GPU_CMD_RESOURCE_CREATE_2D;
    cmd.hdr.flags = 0;
    cmd.hdr.fence_id = 0;
    cmd.hdr.ctx_id = 0;
    cmd.resource_id = resource_id;
    cmd.format = format;
    cmd.width = width;
    cmd.height = height;

    return virtio_gpu_cmd_submit(sc, &cmd.hdr, sizeof(cmd), &resp, sizeof(resp), 1000);
}

/*
 * RESOURCE_UNREF command - free a resource
 */
int
virtio_gpu_cmd_resource_unref(struct virtio_gpu_softc *sc,
                             uint32_t resource_id)
{
    struct virtio_gpu_resource_unref cmd;
    struct virtio_gpu_ctrl_hdr resp;

    if(!sc)
        return -1;

    memset(&cmd, 0, sizeof(cmd));
    cmd.hdr.type = VIRTIO_GPU_CMD_RESOURCE_UNREF;
    cmd.resource_id = resource_id;

    return virtio_gpu_cmd_submit(sc, &cmd.hdr, sizeof(cmd), &resp, sizeof(resp), 1000);
}

/*
 * SET_SCANOUT command - attach resource to display
 */
int
virtio_gpu_cmd_set_scanout(struct virtio_gpu_softc *sc,
                          uint32_t scanout_id,
                          uint32_t resource_id,
                          uint32_t x, uint32_t y,
                          uint32_t width, uint32_t height)
{
    struct virtio_gpu_set_scanout cmd;
    struct virtio_gpu_ctrl_hdr resp;

    if(!sc)
        return -1;

    memset(&cmd, 0, sizeof(cmd));
    cmd.hdr.type = VIRTIO_GPU_CMD_SET_SCANOUT;
    cmd.scanout_id = scanout_id;
    cmd.resource_id = resource_id;
    cmd.r.x = x;
    cmd.r.y = y;
    cmd.r.width = width;
    cmd.r.height = height;

    return virtio_gpu_cmd_submit(sc, &cmd.hdr, sizeof(cmd), &resp, sizeof(resp), 1000);
}

/*
 * TRANSFER_TO_HOST_2D command - sync pixels to GPU
 */
int
virtio_gpu_cmd_transfer_to_host_2d(struct virtio_gpu_softc *sc,
                                  uint32_t resource_id,
                                  uint32_t x, uint32_t y,
                                  uint32_t width, uint32_t height,
                                  uint64_t offset)
{
    struct virtio_gpu_transfer_to_host_2d cmd;
    struct virtio_gpu_ctrl_hdr resp;

    if(!sc)
        return -1;

    memset(&cmd, 0, sizeof(cmd));
    cmd.hdr.type = VIRTIO_GPU_CMD_TRANSFER_TO_HOST_2D;
    cmd.resource_id = resource_id;
    cmd.offset = offset;
    cmd.r.x = x;
    cmd.r.y = y;
    cmd.r.width = width;
    cmd.r.height = height;

    return virtio_gpu_cmd_submit(sc, &cmd.hdr, sizeof(cmd), &resp, sizeof(resp), 1000);
}

/*
 * RESOURCE_FLUSH command - finalize rendering
 */
int
virtio_gpu_cmd_resource_flush(struct virtio_gpu_softc *sc,
                             uint32_t resource_id,
                             uint32_t x, uint32_t y,
                             uint32_t width, uint32_t height)
{
    struct virtio_gpu_resource_flush cmd;
    struct virtio_gpu_ctrl_hdr resp;

    if(!sc)
        return -1;

    memset(&cmd, 0, sizeof(cmd));
    cmd.hdr.type = VIRTIO_GPU_CMD_RESOURCE_FLUSH;
    cmd.resource_id = resource_id;
    cmd.r.x = x;
    cmd.r.y = y;
    cmd.r.width = width;
    cmd.r.height = height;

    return virtio_gpu_cmd_submit(sc, &cmd.hdr, sizeof(cmd), &resp, sizeof(resp), 1000);
}

/*
 * RESOURCE_ATTACH_BACKING command - map host memory to GPU resource
 */
int
virtio_gpu_cmd_resource_attach_backing(struct virtio_gpu_softc *sc,
                                      uint32_t resource_id,
                                      struct virtio_gpu_mem_entry *entries,
                                      uint32_t nr_entries)
{
    struct virtio_gpu_resource_attach_backing *cmd;
    struct virtio_gpu_ctrl_hdr resp;
    uint cmd_size;
    int rc;

    if(!sc || !entries || nr_entries == 0)
        return -1;

    if(nr_entries > VIRTIO_GPU_MAX_BACKING_PAGES)
        return -1;

    /* Allocate command with variable number of entries */
    cmd_size = sizeof(*cmd) + (nr_entries - 1) * sizeof(struct virtio_gpu_mem_entry);
    cmd = (struct virtio_gpu_resource_attach_backing *)kalloc();
    if(!cmd)
        return -1;

    memset(cmd, 0, cmd_size);
    cmd->hdr.type = VIRTIO_GPU_CMD_RESOURCE_ATTACH_BACKING;
    cmd->resource_id = resource_id;
    cmd->nr_entries = nr_entries;
    memmove(cmd->entries, entries, nr_entries * sizeof(struct virtio_gpu_mem_entry));

    rc = virtio_gpu_cmd_submit(sc, &cmd->hdr, cmd_size, &resp, sizeof(resp), 1000);

    kfree((void *)cmd);
    return rc;
}

/*
 * Wait for a response to a command (with timeout)
 */
void *
virtio_gpu_wait_response(struct virtio_gpu_softc *sc,
                        uint32_t fence_id, uint32_t timeout_ms)
{
    (void)sc;
    (void)fence_id;
    (void)timeout_ms;

    /* TODO: Implement response waiting with timeout */
    return 0;
}

static int
virtio_gpu_format_from_pixfmt(uint pixfmt)
{
    switch(pixfmt) {
    case PIXFMT_XRGB8888:
        return VIRTIO_GPU_FORMAT_B8G8R8X8_UNORM;
    default:
        return 0;
    }
}

static int
virtio_gpu_resource_index_for_fb(struct virtio_gpu_softc *sc, struct framebuffer *fb)
{
    int i;

    if(!sc || !fb)
        return -1;

    for(i = 0; i < VIRTIO_GPU_MAX_RESOURCES; i++) {
        if(sc->resources[i].in_use && sc->resources[i].pixels == fb->pixels)
            return i;
    }
    return -1;
}

static int
virtio_gpu_alloc_resource_slot(struct virtio_gpu_softc *sc)
{
    int i;
    for(i = 0; i < VIRTIO_GPU_MAX_RESOURCES; i++) {
        if(!sc->resources[i].in_use)
            return i;
    }
    return -1;
}

static struct framebuffer *
virtio_gpu_display_create_framebuffer(struct display_device *dev,
                                      uint width, uint height, uint pixfmt)
{
    struct virtio_gpu_softc *sc;
    struct framebuffer *fb;
    struct virtio_gpu_mem_entry entry;
    int slot;
    int resource_id;
    int format;

    if(!dev)
        return 0;
    sc = (struct virtio_gpu_softc *)dev->driver_data;
    if(!sc)
        return 0;

    format = virtio_gpu_format_from_pixfmt(pixfmt);
    if(format == 0) {
        virtio_gpu_uartlog("virtio_gpu: unsupported framebuffer pixfmt\n");
        return 0;
    }

    fb = fb_alloc(width, height, pixfmt);
    if(!fb) {
        virtio_gpu_uartlog("virtio_gpu: fb_alloc failed\n");
        return 0;
    }

    acquire(&sc->lock);
    slot = virtio_gpu_alloc_resource_slot(sc);
    release(&sc->lock);
    if(slot < 0) {
        virtio_gpu_uartlog("virtio_gpu: no free resource slots\n");
        fb_free(fb);
        return 0;
    }

    resource_id = slot + 1;
    if(virtio_gpu_cmd_resource_create_2d(sc, (uint32_t)resource_id,
                                         (uint32_t)format,
                                         (uint32_t)width,
                                         (uint32_t)height) < 0) {
        virtio_gpu_uartlog("virtio_gpu: RESOURCE_CREATE_2D failed\n");
        fb_free(fb);
        return 0;
    }

    entry.addr = (uint64_t)fb->phys_addr;
    entry.length = fb->size_bytes;
    entry.padding = 0;
    if(virtio_gpu_cmd_resource_attach_backing(sc, (uint32_t)resource_id,
                                              &entry, 1) < 0) {
        virtio_gpu_uartlog("virtio_gpu: RESOURCE_ATTACH_BACKING failed\n");
        virtio_gpu_cmd_resource_unref(sc, (uint32_t)resource_id);
        fb_free(fb);
        return 0;
    }

    acquire(&sc->lock);
    sc->resources[slot].resource_id = (uint32_t)resource_id;
    sc->resources[slot].format = (uint32_t)format;
    sc->resources[slot].width = width;
    sc->resources[slot].height = height;
    sc->resources[slot].refcount = 1;
    sc->resources[slot].pixels = fb->pixels;
    sc->resources[slot].phys_addr = fb->phys_addr;
    sc->resources[slot].in_use = 1;
    release(&sc->lock);

    return fb;
}

static void
virtio_gpu_display_destroy_framebuffer(struct display_device *dev,
                                       struct framebuffer *fb)
{
    struct virtio_gpu_softc *sc;
    int slot;

    if(!dev || !fb)
        return;
    sc = (struct virtio_gpu_softc *)dev->driver_data;
    if(!sc)
        return;

    acquire(&sc->lock);
    slot = virtio_gpu_resource_index_for_fb(sc, fb);
    if(slot >= 0) {
        uint32_t rid = sc->resources[slot].resource_id;
        sc->resources[slot].in_use = 0;
        release(&sc->lock);
        virtio_gpu_cmd_resource_unref(sc, rid);
    } else {
        release(&sc->lock);
    }

    fb_free(fb);
}

static int
virtio_gpu_display_set_scanout(struct display_device *dev,
                               struct display_crtc *crtc,
                               struct framebuffer *fb)
{
    struct virtio_gpu_softc *sc;
    int slot;
    uint32_t rid;

    if(!dev || !crtc || !fb)
        return -1;
    sc = (struct virtio_gpu_softc *)dev->driver_data;
    if(!sc)
        return -1;

    acquire(&sc->lock);
    slot = virtio_gpu_resource_index_for_fb(sc, fb);
    if(slot < 0) {
        release(&sc->lock);
        return -1;
    }
    rid = sc->resources[slot].resource_id;
    release(&sc->lock);

    if(virtio_gpu_cmd_set_scanout(sc, 0, rid, 0, 0,
                                  fb->width, fb->height) < 0) {
        virtio_gpu_uartlog("virtio_gpu: SET_SCANOUT failed\n");
        return -1;
    }

    crtc->front = fb;
    crtc->enabled = 1;
    crtc->x = 0;
    crtc->y = 0;
    dev->current_scanout = fb;
    return 0;
}

static int
virtio_gpu_display_flush_region(struct display_device *dev,
                                struct framebuffer *fb,
                                struct dirty_rect *region)
{
    struct virtio_gpu_softc *sc;
    int slot;
    uint32_t rid;
    uint32_t x;
    uint32_t y;
    uint32_t w;
    uint32_t h;
    uint64_t off;

    if(!dev || !fb || !region)
        return -1;
    sc = (struct virtio_gpu_softc *)dev->driver_data;
    if(!sc)
        return -1;

    x = (uint32_t)region->left;
    y = (uint32_t)region->top;
    if(region->right < region->left || region->bottom < region->top)
        return 0;
    w = (uint32_t)(region->right - region->left + 1);
    h = (uint32_t)(region->bottom - region->top + 1);

    acquire(&sc->lock);
    slot = virtio_gpu_resource_index_for_fb(sc, fb);
    if(slot < 0) {
        release(&sc->lock);
        return -1;
    }
    rid = sc->resources[slot].resource_id;
    release(&sc->lock);

    off = (uint64_t)y * (uint64_t)fb->stride + (uint64_t)x * (uint64_t)fb->bpp;
    fb_sync_for_device(fb);

    if(virtio_gpu_cmd_transfer_to_host_2d(sc, rid, x, y, w, h, off) < 0)
        return -1;
    if(virtio_gpu_cmd_resource_flush(sc, rid, x, y, w, h) < 0)
        return -1;

    return 0;
}

static int
virtio_gpu_display_flush(struct display_device *dev, struct framebuffer *fb)
{
    struct dirty_rect rect;
    int rc;

    if(!dev || !fb)
        return -1;

    if(fb_is_dirty(fb)) {
        fb_get_dirty_rect(fb, &rect);
    } else {
        rect.left = 0;
        rect.top = 0;
        rect.right = (int)fb->width - 1;
        rect.bottom = (int)fb->height - 1;
    }

    rc = virtio_gpu_display_flush_region(dev, fb, &rect);
    if(rc == 0)
        fb_clear_dirty(fb);
    return rc;
}

/* Initialization hook - called from main kernel initialization */
void
virtio_gpu_init(void)
{
    int found;

    initlock(&virtio_gpu_lock, "virtio_gpu");
    virtio_gpu_count = 0;
    found = 0;

    BOOTDBG("virtio_gpu: subsystem initialized\n");

    /* Look for virtio-gpu PCI devices (same model as virtio_blk/net). */
    for (int i = 0; i < pci_device_count(); i++) {
        struct pci_dev *dev = pci_get_device(i);
        if (!dev)
            continue;

        if (dev->vendor_id == PCI_VENDOR_VIRTIO &&
            (dev->device_id == 0x1050 ||
             (dev->device_id >= 0x1000 && dev->device_id <= 0x103F &&
              dev->device_id - 0x0FFF == VIRTIO_DEV_GPU))) {
            found = 1;
            if (virtio_gpu_probe(dev) < 0) {
                cprintf("virtio_gpu: probe failed at %d:%d.%d id=%x:%x\n",
                        dev->bus, dev->slot, dev->func,
                        dev->vendor_id, dev->device_id);
            }
        }
    }

    if (!found)
        cprintf("virtio_gpu: no compatible PCI device\n");
}
