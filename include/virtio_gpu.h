/*
 * VirtIO GPU Device Driver for auxv6
 *
 * Implements the VirtIO GPU device specification.
 * Supports basic 2D rendering and scanout operations.
 *
 * Reference: VirtIO 1.1 Specification Section 5.7
 * See also: Linux drivers/gpu/drm/virtio/
 */

#ifndef _VIRTIO_GPU_H_
#define _VIRTIO_GPU_H_

#include "types.h"
#include "virtio.h"
#include "graphics/display.h"
#include "graphics/framebuffer.h"

/* VirtIO GPU device ID */
#define VIRTIO_DEV_GPU      16

/* VirtIO GPU feature bits */
#define VIRTIO_GPU_F_VIRGL          (1ULL << 0)  /* 3D virgl support */
#define VIRTIO_GPU_F_EDID           (1ULL << 1)  /* EDID support */
#define VIRTIO_GPU_F_RESOURCE_UUID  (1ULL << 2)  /* UUID for resources */

/* VirtIO GPU command types */
#define VIRTIO_GPU_CMD_GET_DISPLAY_INFO     0x0100
#define VIRTIO_GPU_CMD_RESOURCE_CREATE_2D   0x0101
#define VIRTIO_GPU_CMD_RESOURCE_UNREF       0x0102
#define VIRTIO_GPU_CMD_SET_SCANOUT          0x0103
#define VIRTIO_GPU_CMD_RESOURCE_FLUSH       0x0104
#define VIRTIO_GPU_CMD_TRANSFER_TO_HOST_2D  0x0105
#define VIRTIO_GPU_CMD_RESOURCE_ATTACH_BACKING 0x0106
#define VIRTIO_GPU_CMD_RESOURCE_DETACH_BACKING 0x0107
#define VIRTIO_GPU_CMD_GET_CAPSET_INFO      0x0108
#define VIRTIO_GPU_CMD_GET_CAPSET           0x0109
#define VIRTIO_GPU_CMD_GET_EDID             0x010a
#define VIRTIO_GPU_CMD_RESOURCE_CREATE_3D   0x0140
#define VIRTIO_GPU_CMD_TRANSFER_TO_HOST_3D  0x0141
#define VIRTIO_GPU_CMD_TRANSFER_FROM_HOST_3D 0x0142
#define VIRTIO_GPU_CMD_SUBMIT_3D            0x0143
#define VIRTIO_GPU_CMD_RESOURCE_MAP_BLOB    0x0144
#define VIRTIO_GPU_CMD_RESOURCE_UNMAP_BLOB  0x0145

/* Response types */
#define VIRTIO_GPU_RESP_OK_NODATA           0x1100
#define VIRTIO_GPU_RESP_OK_DISPLAY_INFO     0x1101
#define VIRTIO_GPU_RESP_OK_CAPSET_INFO      0x1102
#define VIRTIO_GPU_RESP_OK_CAPSET           0x1103
#define VIRTIO_GPU_RESP_OK_EDID             0x1104
#define VIRTIO_GPU_RESP_OK_RESOURCE_UUID    0x1105
#define VIRTIO_GPU_RESP_OK_MAP_INFO         0x1106
#define VIRTIO_GPU_RESP_ERR_UNSPEC          0x1200
#define VIRTIO_GPU_RESP_ERR_OUT_OF_MEMORY   0x1201
#define VIRTIO_GPU_RESP_ERR_INVALID_SCANOUT_ID 0x1202
#define VIRTIO_GPU_RESP_ERR_INVALID_RESOURCE_ID 0x1203
#define VIRTIO_GPU_RESP_ERR_INVALID_CONTEXT_ID 0x1204
#define VIRTIO_GPU_RESP_ERR_INVALID_PARAMETER 0x1205

/* Queue indices */
#define VIRTIO_GPU_Q_CONTROL    0
#define VIRTIO_GPU_Q_CURSOR     1
#define VIRTIO_GPU_Q_RENDER     2 /* Optional */

/* Formats */
#define VIRTIO_GPU_FORMAT_B8G8R8A8_UNORM    1
#define VIRTIO_GPU_FORMAT_B8G8R8X8_UNORM    2
#define VIRTIO_GPU_FORMAT_A8R8G8B8_UNORM    3
#define VIRTIO_GPU_FORMAT_X8R8G8B8_UNORM    4
#define VIRTIO_GPU_FORMAT_R8G8B8A8_UNORM    67
#define VIRTIO_GPU_FORMAT_X8B8G8R8_UNORM    68
#define VIRTIO_GPU_FORMAT_A8B8G8R8_UNORM    121
#define VIRTIO_GPU_FORMAT_R8G8B8X8_UNORM    134

/* Resource type */
#define VIRTIO_GPU_RESOURCE_TYPE_2D         1
#define VIRTIO_GPU_RESOURCE_TYPE_3D         2
#define VIRTIO_GPU_RESOURCE_TYPE_BLOB       3

/* Command request header (common to all commands) */
struct virtio_gpu_ctrl_hdr {
    uint32_t type;
    uint32_t flags;
    uint64_t fence_id;
    uint32_t ctx_id;
    uint32_t padding;
} __attribute__((packed));

/* GET_DISPLAY_INFO command */
struct virtio_gpu_get_display_info {
    struct virtio_gpu_ctrl_hdr hdr;
} __attribute__((packed));

/* Display mode (in display info response) */
struct virtio_gpu_display_one {
    struct {
        uint32_t x;
        uint32_t y;
        uint32_t width;
        uint32_t height;
    } r;
    uint32_t enabled;
    uint32_t flags;
} __attribute__((packed));

/* GET_DISPLAY_INFO response */
#define VIRTIO_GPU_MAX_SCANOUTS 16
struct virtio_gpu_resp_display_info {
    struct virtio_gpu_ctrl_hdr hdr;
    struct virtio_gpu_display_one pmodes[VIRTIO_GPU_MAX_SCANOUTS];
} __attribute__((packed));

/* RESOURCE_CREATE_2D command */
struct virtio_gpu_resource_create_2d {
    struct virtio_gpu_ctrl_hdr hdr;
    uint32_t resource_id;
    uint32_t format;
    uint32_t width;
    uint32_t height;
} __attribute__((packed));

/* RESOURCE_UNREF command */
struct virtio_gpu_resource_unref {
    struct virtio_gpu_ctrl_hdr hdr;
    uint32_t resource_id;
    uint32_t padding;
} __attribute__((packed));

/* SET_SCANOUT command */
struct virtio_gpu_set_scanout {
    struct virtio_gpu_ctrl_hdr hdr;
    struct {
        uint32_t x;
        uint32_t y;
        uint32_t width;
        uint32_t height;
    } r;
    uint32_t scanout_id;
    uint32_t resource_id;
} __attribute__((packed));

/* RESOURCE_FLUSH command */
struct virtio_gpu_resource_flush {
    struct virtio_gpu_ctrl_hdr hdr;
    struct {
        uint32_t x;
        uint32_t y;
        uint32_t width;
        uint32_t height;
    } r;
    uint32_t resource_id;
    uint32_t padding;
} __attribute__((packed));

/* TRANSFER_TO_HOST_2D command */
struct virtio_gpu_transfer_to_host_2d {
    struct virtio_gpu_ctrl_hdr hdr;
    struct {
        uint32_t x;
        uint32_t y;
        uint32_t width;
        uint32_t height;
    } r;
    uint64_t offset;
    uint32_t resource_id;
    uint32_t padding;
} __attribute__((packed));

/* RESOURCE_ATTACH_BACKING command */
#define VIRTIO_GPU_MAX_BACKING_PAGES 4096
struct virtio_gpu_mem_entry {
    uint64_t addr;
    uint32_t length;
    uint32_t padding;
} __attribute__((packed));

struct virtio_gpu_resource_attach_backing {
    struct virtio_gpu_ctrl_hdr hdr;
    uint32_t resource_id;
    uint32_t nr_entries;
    struct virtio_gpu_mem_entry entries[1];  /* variable size */
} __attribute__((packed));

/* Driver state */
struct virtio_gpu_softc {
    struct virtio_dev vdev;
    struct spinlock lock;
    uint8_t irq_allocated;
    
    /* Display info */
    struct display_device *display_dev;
    uint num_scanouts;
    struct {
        uint enabled;
        uint width, height;
    } scanouts[VIRTIO_GPU_MAX_SCANOUTS];
    
    /* Resource tracking */
    #define VIRTIO_GPU_MAX_RESOURCES 64
    struct {
        uint32_t resource_id;
        uint32_t format;
        uint32_t width, height;
        uint32_t refcount;
        void *pixels;
        uint phys_addr;
        int in_use;
    } resources[VIRTIO_GPU_MAX_RESOURCES];
    
    /* Pending responses */
    struct spinlock resp_lock;
    struct {
        uint32_t fence_id;
        uint32_t type;
        void *response;
        int ready;
    } pending_resps[16];
};

/* Function prototypes */

/* Driver lifecycle */
void virtio_gpu_init(void);
int virtio_gpu_probe(struct pci_dev *pci);
void virtio_gpu_remove(struct virtio_gpu_softc *sc);

/* Command submission */
int virtio_gpu_cmd_get_display_info(struct virtio_gpu_softc *sc,
                                    struct virtio_gpu_resp_display_info *resp);
int virtio_gpu_cmd_resource_create_2d(struct virtio_gpu_softc *sc,
                                      uint32_t resource_id, uint32_t format,
                                      uint32_t width, uint32_t height);
int virtio_gpu_cmd_resource_unref(struct virtio_gpu_softc *sc,
                                  uint32_t resource_id);
int virtio_gpu_cmd_set_scanout(struct virtio_gpu_softc *sc,
                               uint32_t scanout_id,
                               uint32_t resource_id,
                               uint32_t x, uint32_t y,
                               uint32_t width, uint32_t height);
int virtio_gpu_cmd_transfer_to_host_2d(struct virtio_gpu_softc *sc,
                                       uint32_t resource_id,
                                       uint32_t x, uint32_t y,
                                       uint32_t width, uint32_t height,
                                       uint64_t offset);
int virtio_gpu_cmd_resource_flush(struct virtio_gpu_softc *sc,
                                  uint32_t resource_id,
                                  uint32_t x, uint32_t y,
                                  uint32_t width, uint32_t height);
int virtio_gpu_cmd_resource_attach_backing(struct virtio_gpu_softc *sc,
                                           uint32_t resource_id,
                                           struct virtio_gpu_mem_entry *entries,
                                           uint32_t nr_entries);

/* Response handling */
void *virtio_gpu_wait_response(struct virtio_gpu_softc *sc,
                               uint32_t fence_id, uint32_t timeout_ms);

#endif /* _VIRTIO_GPU_H_ */
