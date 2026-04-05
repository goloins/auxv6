/*
 * Framebuffer Core Abstraction for auxv6
 *
 * Generic framebuffer management independent of hardware driver.
 * Handles software blitting, dirty-rectangle tracking, and scanout.
 *
 * Design inspired by Linux DRM framebuffer abstraction and X11 Xrender.
 */

#ifndef _GRAPHICS_FRAMEBUFFER_H_
#define _GRAPHICS_FRAMEBUFFER_H_

#include "types.h"

/* Pixel formats */
#define PIXFMT_RGB565       0x01    /* 16-bit: RRR GGGGG BBBBB */
#define PIXFMT_BGR565       0x02    /* 16-bit: BBB GGGGG RRRRR */
#define PIXFMT_XRGB8888     0x04    /* 32-bit: XXXX RRRR RRRR GGGG GGGG BBBB BBBB */
#define PIXFMT_XBGR8888     0x05    /* 32-bit: XXXX BBBB BBBB GGGG GGGG RRRR RRRR */
#define PIXFMT_ARGB8888     0x06    /* 32-bit: AAAA RRRR RRRR GGGG GGGG BBBB BBBB */
#define PIXFMT_ABGR8888     0x07    /* 32-bit: AAAA BBBB BBBB GGGG GGGG RRRR RRRR */

/* Dirty rectangle tracking */
struct dirty_rect {
    int top;
    int left;
    int bottom;
    int right;
};

#define FB_MAX_DIRTY_RECTS 8

/* Framebuffer structure - represents a drawable pixel region */
struct framebuffer {
    /* Buffer metadata */
    void *pixels;           /* Virtual address of pixel data */
    uint phys_addr;         /* Physical address (for DMA) */
    uint width;
    uint height;
    uint stride;            /* bytes per scanline */
    uint pixfmt;            /* PIXFMT_* */
    uint bpp;               /* bytes per pixel (derived from pixfmt) */
    uint size_bytes;        /* width * height * bpp */
    
    /* Dirty tracking */
    int dirty_top;
    int dirty_left;
    int dirty_bottom;
    int dirty_right;
    int dirty;              /* 0 if clean, 1 if any dirty pixels */
    struct dirty_rect dirty_rects[FB_MAX_DIRTY_RECTS];
    int dirty_rect_count;
    
    /* Ownership */
    int is_dma_coherent;    /* 1 if CPU/GPU coherent, 0 if needs sync */
    int is_mapped_user;     /* 1 if mapped to userspace */
    void *user_vaddr;       /* userspace mapping (if applicable) */
    
    /* Reference counting */
    int ref_count;
    struct spinlock lock;
};

/* Scanout buffer - what's currently visible */
struct scanout_buffer {
    struct framebuffer *fb;
    uint offset;            /* Offset within driver's VRAM */
    int is_active;          /* Currently being scanned out */
};

/* Color structure - device-independent */
struct color {
    uchar r, g, b, a;
};

/* Drawing context */
struct draw_context {
    uint fg_color;          /* foreground color in device pixel format */
    uint bg_color;          /* background color */
    int clip_enabled;
    struct dirty_rect clip;
};

/* Function prototypes */

/* Framebuffer management */
struct framebuffer *fb_alloc(uint width, uint height, uint pixfmt);
void fb_free(struct framebuffer *fb);
void fb_ref(struct framebuffer *fb);
void fb_unref(struct framebuffer *fb);

/* Pixel format utilities */
int fb_pixfmt_to_bpp(uint pixfmt);
uint fb_color_to_pixel(uint pixfmt, struct color c);
struct color fb_pixel_to_color(uint pixfmt, uint pixel);
int fb_convert_pixel(uint src_fmt, uint dst_fmt, uint src_pixel, uint *dst_pixel);
void *fb_convert_buffer(void *src, uint src_fmt, void *dst, uint dst_fmt,
                        uint width, uint height);

/* Dirty rectangle tracking */
void fb_mark_dirty(struct framebuffer *fb, int x, int y, uint w, uint h);
void fb_mark_dirty_rect(struct framebuffer *fb, struct dirty_rect *rect);
void fb_clear_dirty(struct framebuffer *fb);
int fb_is_dirty(struct framebuffer *fb);
void fb_get_dirty_rect(struct framebuffer *fb, struct dirty_rect *out);
int fb_get_dirty_rect_count(struct framebuffer *fb);
int fb_get_dirty_rect_at(struct framebuffer *fb, int index, struct dirty_rect *out);

/* Low-level pixel operations */
void fb_set_pixel(struct framebuffer *fb, int x, int y, uint pixel);
uint fb_get_pixel(struct framebuffer *fb, int x, int y);

/* Clipped drawing primitives */
void fb_fill_rect(struct framebuffer *fb, int x, int y, uint w, uint h, uint pixel);
void fb_blit_rect(struct framebuffer *src, int src_x, int src_y,
                  struct framebuffer *dst, int dst_x, int dst_y,
                  uint w, uint h);
void fb_fill_pattern(struct framebuffer *fb, int x, int y, uint w, uint h,
                     const void *pattern, uint pattern_pitch);

/* Advanced blitting with clipping and alpha */
void fb_blit_clipped(struct framebuffer *src, struct dirty_rect *src_region,
                     struct framebuffer *dst, int dst_x, int dst_y);
void fb_blit_alpha(struct framebuffer *src, struct framebuffer *dst,
                   int dst_x, int dst_y, uchar alpha);

/* Text rendering (see render.h for full API) */
struct font;
int fb_draw_glyph(struct framebuffer *fb, const struct font *font,
                  int x, int y, uint codepoint, uint fg, uint bg);

/* Synchronization */
void fb_flush(struct framebuffer *fb);
void fb_sync_for_device(struct framebuffer *fb);
void fb_sync_for_cpu(struct framebuffer *fb);

/* Scanout management */
struct scanout_buffer *fb_create_scanout(struct framebuffer *fb);
void fb_destroy_scanout(struct scanout_buffer *sb);

/* Memory mapping */
int fb_map_user(struct framebuffer *fb);
int fb_unmap_user(struct framebuffer *fb);

#endif /* _GRAPHICS_FRAMEBUFFER_H_ */
