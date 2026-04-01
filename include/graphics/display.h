/*
 * Display Device Abstraction for auxv6
 *
 * Generic interface for display hardware (GPUs, framebuffer devices).
 * Defines mode setting, buffer management, and scanout operations.
 *
 * Design inspired by Linux KMS (Kernel Mode Setting) and DRM subsystem.
 */

#ifndef _GRAPHICS_DISPLAY_H_
#define _GRAPHICS_DISPLAY_H_

#include "types.h"
#include "graphics/framebuffer.h"

/* Connector types */
#define CONN_UNKNOWN    0
#define CONN_VGA        1
#define CONN_HDMI       2
#define CONN_DP         3
#define CONN_INTERNAL   4
#define CONN_USB        5
#define CONN_VIRTUAL    6

/* Encoder types */
#define ENC_NONE        0
#define ENC_DAC         1
#define ENC_TMDS        2      /* HDMI */
#define ENC_LVDS        3
#define ENC_DP          4

/* Display mode information */
struct display_mode {
    uint width;
    uint height;
    uint refresh;               /* refresh rate in Hz (0 = don't care) */
    uint pixfmt;                /* PIXFMT_* from framebuffer.h */
    uint vdisplay, vblank_start, vblank_end, vsync_start, vsync_end;
    uint hdisplay, hblank_start, hblank_end, hsync_start, hsync_end;
    uint dotclock;              /* pixel clock in kHz */
    int interlaced;
    int doublescan;
    char name[32];
};

/* Connector status */
#define CONN_STATUS_UNKNOWN     0
#define CONN_STATUS_CONNECTED   1
#define CONN_STATUS_DISCONNECTED 2

/* Display connector - output socket */
struct display_connector {
    uint connector_id;
    int type;                   /* CONN_* */
    int status;                 /* CONN_STATUS_* */
    struct display_mode *modes;
    uint num_modes;
    uint preferred_mode;
    uint physical_width_mm;
    uint physical_height_mm;
    char name[32];
    
    /* Associated encoder if any */
    uint encoder_id;
};

/* Encoder - converts video signal */
struct display_encoder {
    uint encoder_id;
    int type;                   /* ENC_* */
    uint connector_id;
};

/* CRTC - controls one output pipe (cathode ray tube controller) */
struct display_crtc {
    uint crtc_id;
    uint connector_id;
    struct display_mode *mode;
    int enabled;
    int x, y;                   /* offset within framebuffer */
    struct framebuffer *front;  /* scanout buffer */
    struct framebuffer *back;   /* shadow buffer (optional) */
};

/* Display device - GPU or framebuffer device */
struct display_device {
    /* Identity */
    uint device_id;
    char name[32];
    int is_primary;
    
    /* Hardware capabilities */
    uint max_width, max_height;
    uint min_width, min_height;
    uint max_framebuffers;
    uint vram_size;
    void *vram_addr;
    uint vram_phys;
    
    /* Mode/resource tracking */
    struct display_connector *connectors;
    uint num_connectors;
    struct display_encoder *encoders;
    uint num_encoders;
    struct display_crtc *crtcs;
    uint num_crtcs;
    struct framebuffer **framebuffers;
    uint num_framebuffers;
    
    /* Current state */
    struct display_mode *current_mode;
    struct framebuffer *current_scanout;
    
    /* Driver interface */
    const struct display_device_ops *ops;
    void *driver_data;
    
    /* Reference counting & synchronization */
    int ref_count;
    struct spinlock lock;
};

/* Display device operations - hardware-specific */
struct display_device_ops {
    /* Lifecycle */
    int (*probe)(struct display_device *dev);
    int (*probe_connector)(struct display_device *dev, struct display_connector *conn);
    
    /* Mode setting */
    int (*set_mode)(struct display_device *dev, struct display_crtc *crtc,
                    struct display_mode *mode);
    int (*get_modes)(struct display_device *dev, struct display_connector *conn,
                     struct display_mode **out, int *count);
    
    /* Buffer management */
    struct framebuffer *(*create_framebuffer)(struct display_device *dev,
                                              uint width, uint height, uint pixfmt);
    void (*destroy_framebuffer)(struct display_device *dev, struct framebuffer *fb);
    
    /* Scanout */
    int (*set_scanout)(struct display_device *dev, struct display_crtc *crtc,
                       struct framebuffer *fb);
    int (*pageflip)(struct display_device *dev, struct display_crtc *crtc,
                    struct framebuffer *fb);
    
    /* Rendering/flush */
    int (*flush_region)(struct display_device *dev, struct framebuffer *fb,
                        struct dirty_rect *region);
    int (*flush)(struct display_device *dev, struct framebuffer *fb);
    
    /* Synchronization */
    int (*wait_vsync)(struct display_device *dev, struct display_crtc *crtc);
    int (*fence_create)(struct display_device *dev, void **fence_out);
    int (*fence_wait)(struct display_device *dev, void *fence);
    
    /* Power management */
    int (*suspend)(struct display_device *dev);
    int (*resume)(struct display_device *dev);
    
    /* Cleanup */
    void (*remove)(struct display_device *dev);
};

/* Function prototypes */

/* Device management */
void display_init(void);
struct display_device *display_device_alloc(void);
void display_device_free(struct display_device *dev);
int display_device_register(struct display_device *dev);
int display_device_unregister(struct display_device *dev);
struct display_device *display_device_get(uint id);
void display_device_ref(struct display_device *dev);
void display_device_unref(struct display_device *dev);

/* Probing and enumeration */
int display_probe_all(void);
struct display_device *display_get_primary(void);
int display_num_devices(void);
struct display_device *display_get_device(int index);

/* Mode negotiation */
int display_get_preferred_mode(struct display_device *dev,
                               struct display_connector *conn,
                               struct display_mode *out);
int display_set_mode(struct display_device *dev, struct display_crtc *crtc,
                     struct display_mode *mode);

/* Framebuffer and scanout */
struct framebuffer *display_create_framebuffer(struct display_device *dev,
                                               uint width, uint height,
                                               uint pixfmt);
int display_set_scanout(struct display_device *dev, struct display_crtc *crtc,
                        struct framebuffer *fb);
int display_pageflip(struct display_device *dev, struct display_crtc *crtc,
                     struct framebuffer *fb);

/* Rendering */
int display_flush(struct display_device *dev, struct framebuffer *fb);
int display_flush_region(struct display_device *dev, struct framebuffer *fb,
                        int x, int y, uint w, uint h);

/* Synchronization */
int display_wait_vsync(struct display_device *dev, struct display_crtc *crtc);

/* Connector/encoder queries */
int display_connector_status(struct display_device *dev,
                             struct display_connector *conn);
int display_get_connector_info(struct display_device *dev, uint conn_id,
                               struct display_connector *out);

/* Utility */
const char *display_connector_type_str(int type);
const char *display_mode_str(struct display_mode *mode, char *buf, int len);

/* Master access control */
int display_set_master(struct display_device *dev);
int display_drop_master(struct display_device *dev);

#endif /* _GRAPHICS_DISPLAY_H_ */
