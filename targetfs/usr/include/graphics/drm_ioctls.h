/*
 * Graphics Device Interface - Character Device Ioctls
 *
 * Userspace API for graphics control and framebuffer management.
 * Based on Linux DRM (Direct Rendering Manager) ioctl interface.
 *
 * Device nodes:
 *  /dev/dri/card0, /dev/dri/card1, ... - primary render devices
 *  /dev/dri/controlD64, ...              - control devices
 *  /dev/fb0, /dev/fb1, ...               - legacy framebuffer devices
 *  /dev/input/event0, ...                - input devices
 */

#ifndef _GRAPHICS_DRM_IOCTLS_H_
#define _GRAPHICS_DRM_IOCTLS_H_

#include "types.h"

/* ioctl magic number */
#define DRM_IOC_MAGIC           'd'

/* ioctl direction bits */
#define DRM_IOC_NONE            0
#define DRM_IOC_READ            1
#define DRM_IOC_WRITE           2
#define DRM_IOC_RDWR            (DRM_IOC_READ | DRM_IOC_WRITE)

/* Macro to define ioctls (simplified version) */
#define DRM_IO(nr)              _IO(DRM_IOC_MAGIC, nr)
#define DRM_IOR(nr, type)       _IOR(DRM_IOC_MAGIC, nr, type)
#define DRM_IOW(nr, type)       _IOW(DRM_IOC_MAGIC, nr, type)
#define DRM_IOWR(nr, type)      _IOWR(DRM_IOC_MAGIC, nr, type)

/* Version information */
#define DRM_VERSION_MAJOR       1
#define DRM_VERSION_MINOR       0
#define DRM_VERSION_PATCHLEVEL  0

struct drm_version {
    uint32_t version_major;
    uint32_t version_minor;
    uint32_t version_patchlevel;
    uint32_t name_len;
    char *name;
    uint32_t date_len;
    char *date;
    uint32_t desc_len;
    char *desc;
};

#define DRM_IOCTL_VERSION       DRM_IOWR(0x00, struct drm_version)

/* Capability queries */
#define DRM_CAP_DUMB_BUFFER     0x1
#define DRM_CAP_VBLANK_HIGH_CRTC 0x2
#define DRM_CAP_DUMB_PREFERRED_DEPTH 0x3
#define DRM_CAP_DUMB_PREFER_SHADOW 0x4
#define DRM_CAP_PRIME           0x5
#define DRM_CAP_TIMESTAMP_MONOTONIC 0x6
#define DRM_CAP_ASYNC_PAGE_FLIP 0x7
#define DRM_CAP_UNIVERSAL_PLANES 0x8

struct drm_get_cap {
    uint64_t capability;
    uint64_t value;
};

#define DRM_IOCTL_GET_CAP       DRM_IOWR(0x01, struct drm_get_cap)

/* Resource enumeration */
struct drm_mode_card_res {
    uint64_t fb_id_ptr;
    uint64_t crtc_id_ptr;
    uint64_t connector_id_ptr;
    uint64_t encoder_id_ptr;
    uint32_t count_fbs;
    uint32_t count_crtcs;
    uint32_t count_connectors;
    uint32_t count_encoders;
    uint32_t min_width, max_width;
    uint32_t min_height, max_height;
};

#define DRM_IOCTL_MODE_GETRESOURCES    DRM_IOWR(0xa0, struct drm_mode_card_res)

/* Connector information */
struct drm_mode_modeinfo {
    uint32_t clock;
    uint16_t hdisplay, hsync_start, hsync_end, htotal, hskew;
    uint16_t vdisplay, vsync_start, vsync_end, vtotal, vscan;
    uint32_t vrefresh;
    uint32_t flags;
    uint32_t type;
    char name[32];
};

struct drm_mode_get_connector {
    uint64_t encoders_ptr;
    uint64_t modes_ptr;
    uint64_t props_ptr;
    uint64_t prop_values_ptr;
    uint32_t count_modes;
    uint32_t count_props;
    uint32_t count_encoders;
    uint32_t encoder_id;
    uint32_t connector_id;
    uint32_t connector_type;
    uint32_t connector_type_id;
    uint32_t connection;
    uint32_t mm_width, mm_height;
    uint32_t subpixel;
    uint32_t pad;
};

#define DRM_IOCTL_MODE_GETCONNECTOR    DRM_IOWR(0xa7, struct drm_mode_get_connector)

/* CRTC (Cathode Ray Tube Controller) information */
struct drm_mode_crtc {
    uint64_t set_connectors_ptr;
    uint32_t count_connectors;
    uint32_t crtc_id;
    uint32_t fb_id;
    int32_t x, y;
    uint32_t gamma_size;
    uint32_t mode_valid;
    struct drm_mode_modeinfo mode;
};

#define DRM_IOCTL_MODE_GETCRTC         DRM_IOWR(0xa1, struct drm_mode_crtc)
#define DRM_IOCTL_MODE_SETCRTC         DRM_IOWR(0xa2, struct drm_mode_crtc)

/* Encoder information */
struct drm_mode_get_encoder {
    uint32_t encoder_id;
    uint32_t encoder_type;
    uint32_t crtc_id;
    uint32_t possible_crtcs;
    uint32_t possible_clones;
};

#define DRM_IOCTL_MODE_GETENCODER      DRM_IOWR(0xa6, struct drm_mode_get_encoder)

/* Framebuffer information */
struct drm_mode_fb_cmd {
    uint32_t fb_id;
    uint32_t width, height;
    uint32_t pitch;
    uint32_t bpp;
    uint32_t depth;
    uint32_t handle;
};

#define DRM_IOCTL_MODE_ADDFB           DRM_IOWR(0xae, struct drm_mode_fb_cmd)
#define DRM_IOCTL_MODE_RMFB            DRM_IOWR(0xaf, uint32_t)
#define DRM_IOCTL_MODE_GETFB           DRM_IOWR(0xad, struct drm_mode_fb_cmd)

/* Generic Memory Allocation (GEM) - for DMA-capable buffers */
struct drm_gem_create {
    uint64_t size;
    uint32_t handle;
};

struct drm_gem_close {
    uint32_t handle;
    uint32_t pad;
};

struct drm_gem_mmap {
    uint32_t handle;
    uint32_t pad;
    uint64_t offset;
};

#define DRM_IOCTL_GEM_CREATE           DRM_IOWR(0x09, struct drm_gem_create)
#define DRM_IOCTL_GEM_CLOSE            DRM_IOW(0x09, struct drm_gem_close)
#define DRM_IOCTL_GEM_MMAP             DRM_IOWR(0x0d, struct drm_gem_mmap)

/* PRIME - buffer export/import */
struct drm_prime_handle {
    uint32_t handle;
    uint32_t flags;
    int32_t fd;
};

#define DRM_IOCTL_PRIME_HANDLE_TO_FD   DRM_IOWR(0x2d, struct drm_prime_handle)
#define DRM_IOCTL_PRIME_FD_TO_HANDLE   DRM_IOWR(0x2e, struct drm_prime_handle)

/* Master control */
struct drm_set_master {
    uint32_t pad;
};

#define DRM_IOCTL_SET_MASTER           DRM_IO(0x1e)
#define DRM_IOCTL_DROP_MASTER          DRM_IO(0x1f)

/* VBlank events */
struct drm_wait_vblank {
    union {
        struct {
            uint32_t type;
            uint32_t sequence;
        } request;
        struct {
            uint32_t type;
            uint32_t sequence;
        } reply;
    } u;
};

#define DRM_IOCTL_WAIT_VBLANK          DRM_IOWR(0x3a, struct drm_wait_vblank)

/* Page flip (atomic mode setting) */
struct drm_mode_crtc_page_flip {
    uint32_t crtc_id;
    uint32_t fb_id;
    uint32_t flags;
    uint32_t reserved;
    uint64_t user_data;
};

#define DRM_IOCTL_MODE_PAGE_FLIP       DRM_IOWR(0xb0, struct drm_mode_crtc_page_flip)

/* Legacy framebuffer device ioctls (FBIOCTL) */
#define FBIOCTL_MAGIC       'F'

struct fb_var_screeninfo {
    uint32_t xres, yres;
    uint32_t xres_virtual, yres_virtual;
    uint32_t xoffset, yoffset;
    uint32_t bits_per_pixel;
    uint32_t grayscale;
    struct {
        uint32_t offset;
        uint32_t length;
        uint32_t msb_right;
    } red, green, blue, transp;
    uint32_t nonstd;
    uint32_t activate;
    uint32_t height, width;         /* physical dimensions in mm */
    uint32_t accel_flags;
    uint32_t pixclock;
    uint32_t left_margin, right_margin;
    uint32_t upper_margin, lower_margin;
    uint32_t hsync_len, vsync_len;
    uint32_t sync;
    uint32_t vmode;
    uint32_t rotate;
    uint32_t colorspace;
    uint32_t reserved[4];
};

struct fb_fix_screeninfo {
    char id[16];
    uint32_t smem_start;
    uint32_t smem_len;
    uint32_t type;
    uint32_t type_aux;
    uint32_t visual;
    uint16_t xpanstep, ypanstep;
    uint16_t ywrapstep;
    uint32_t line_length;
    uint32_t mmio_start;
    uint32_t mmio_len;
    uint32_t accel;
    uint16_t reserved[3];
};

#define FBIOGET_VSCREENINFO    _IOR(FBIOCTL_MAGIC, 0x00, struct fb_var_screeninfo)
#define FBIOPUT_VSCREENINFO    _IOW(FBIOCTL_MAGIC, 0x01, struct fb_var_screeninfo)
#define FBIOGET_FSCREENINFO    _IOR(FBIOCTL_MAGIC, 0x02, struct fb_fix_screeninfo)
#define FBIOPAN_DISPLAY        _IOW(FBIOCTL_MAGIC, 0x06, struct fb_var_screeninfo)

/* Input event device ioctls */
#define INPUT_IOC_MAGIC     'I'

#define EVIOCGVERSION               _IOR(INPUT_IOC_MAGIC, 0x01, int)
#define EVIOCGID                    _IOR(INPUT_IOC_MAGIC, 0x02, uint16_t[4])
#define EVIOCGNAME(len)             _IOC(_IOC_READ, INPUT_IOC_MAGIC, 0x06, len)
#define EVIOCGPHYS(len)             _IOC(_IOC_READ, INPUT_IOC_MAGIC, 0x07, len)
#define EVIOCGPATH(len)             _IOC(_IOC_READ, INPUT_IOC_MAGIC, 0x08, len)

#endif /* _GRAPHICS_DRM_IOCTLS_H_ */
