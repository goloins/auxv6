/*
 * Framebuffer Core Implementation for auxv6
 *
 * Provides generic framebuffer memory management and basic raster operations.
 */

#include "types.h"
#include "defs.h"
#include "memlayout.h"
#include "spinlock.h"
#include "graphics/framebuffer.h"

static void
fb_note(const char *msg)
{
    const char *p;

    if(!msg)
        return;
    for(p = msg; *p; p++)
        uartputc(*p);
}

/* Convert pixel format to bytes per pixel */
int
fb_pixfmt_to_bpp(uint pixfmt)
{
    switch(pixfmt) {
    case PIXFMT_RGB565:
    case PIXFMT_BGR565:
        return 2;
    case PIXFMT_XRGB8888:
    case PIXFMT_XBGR8888:
    case PIXFMT_ARGB8888:
    case PIXFMT_ABGR8888:
        return 4;
    default:
        return 0;
    }
}

/*
 * Allocate a framebuffer structure
 *
 * Allocates the framebuffer metadata and pixel buffer.
 * Physical memory is allocated via dma_alloc for GPU access.
 */
struct framebuffer *
fb_alloc(uint width, uint height, uint pixfmt)
{
    struct framebuffer *fb;
    uint bpp;
    uint size;
    void *pixels;
    uint phys_addr;

    if(width == 0 || height == 0)
        return 0;

    bpp = fb_pixfmt_to_bpp(pixfmt);
    if(bpp == 0) {
        fb_note("fb_alloc: unsupported pixel format\n");
        return 0;
    }

    size = width * height * bpp;
    
    /* Allocate pixel memory via DMA */
    pixels = dma_alloc(size, &phys_addr);
    if(!pixels) {
        fb_note("fb_alloc: dma allocation failed\n");
        return 0;
    }

    /* Allocate framebuffer structure */
    fb = (struct framebuffer *)kalloc();
    if(!fb) {
        dma_free(pixels, size);
        return 0;
    }

    memset(fb, 0, sizeof(*fb));
    fb->pixels = pixels;
    fb->phys_addr = phys_addr;
    fb->width = width;
    fb->height = height;
    fb->stride = width * bpp;
    fb->pixfmt = pixfmt;
    fb->bpp = bpp;
    fb->size_bytes = size;
    fb->ref_count = 1;
    fb->dirty = 1;
    fb->dirty_top = 0;
    fb->dirty_left = 0;
    fb->dirty_bottom = height - 1;
    fb->dirty_right = width - 1;
    fb->dirty_rect_count = 0;
    
    initlock(&fb->lock, "framebuffer");

    return fb;
}

/*
 * Free a framebuffer
 */
void
fb_free(struct framebuffer *fb)
{
    if(!fb)
        return;

    if(fb->pixels) {
        dma_free(fb->pixels, fb->size_bytes);
    }

    kfree((void *)fb);
}

/*
 * Increment framebuffer reference count
 */
void
fb_ref(struct framebuffer *fb)
{
    if(!fb)
        return;

    acquire(&fb->lock);
    fb->ref_count++;
    release(&fb->lock);
}

/*
 * Decrement framebuffer reference count and free if needed
 */
void
fb_unref(struct framebuffer *fb)
{
    if(!fb)
        return;

    acquire(&fb->lock);
    fb->ref_count--;
    if(fb->ref_count <= 0) {
        release(&fb->lock);
        fb_free(fb);
        return;
    }
    release(&fb->lock);
}

/*
 * Mark a rectangular region as dirty (needs redraw)
 */
void
fb_mark_dirty(struct framebuffer *fb, int x, int y, uint w, uint h)
{
    int cw;
    int ch;

    if(!fb)
        return;

    if(w == 0 || h == 0)
        return;

    cw = (int)w;
    ch = (int)h;

    if(x >= (int)fb->width || y >= (int)fb->height)
        return;

    /* Clamp to framebuffer bounds */
    if(x < 0) {
        cw += x;
        x = 0;
    }
    if(y < 0) {
        ch += y;
        y = 0;
    }
    if(x + cw > (int)fb->width)
        cw = (int)fb->width - x;
    if(y + ch > (int)fb->height)
        ch = (int)fb->height - y;

    if(cw <= 0 || ch <= 0)
        return;

    acquire(&fb->lock);

    if(!fb->dirty) {
        /* First dirty region */
        fb->dirty_top = y;
        fb->dirty_left = x;
        fb->dirty_bottom = y + ch - 1;
        fb->dirty_right = x + cw - 1;
        fb->dirty = 1;
        fb->dirty_rects[0].top = fb->dirty_top;
        fb->dirty_rects[0].left = fb->dirty_left;
        fb->dirty_rects[0].bottom = fb->dirty_bottom;
        fb->dirty_rects[0].right = fb->dirty_right;
        fb->dirty_rect_count = 1;
    } else {
        /* Extend existing dirty region */
        if(y < fb->dirty_top) fb->dirty_top = y;
        if(x < fb->dirty_left) fb->dirty_left = x;
        if(y + ch - 1 > fb->dirty_bottom) fb->dirty_bottom = y + ch - 1;
        if(x + cw - 1 > fb->dirty_right) fb->dirty_right = x + cw - 1;
        fb->dirty_rects[0].top = fb->dirty_top;
        fb->dirty_rects[0].left = fb->dirty_left;
        fb->dirty_rects[0].bottom = fb->dirty_bottom;
        fb->dirty_rects[0].right = fb->dirty_right;
        fb->dirty_rect_count = 1;
    }

    release(&fb->lock);
}

/*
 * Mark a dirty rectangle
 */
void
fb_mark_dirty_rect(struct framebuffer *fb, struct dirty_rect *rect)
{
    if(!fb || !rect)
        return;

    fb_mark_dirty(fb, rect->left, rect->top,
                  rect->right - rect->left + 1,
                  rect->bottom - rect->top + 1);
}

/*
 * Get the dirty rectangle bounds
 */
void
fb_get_dirty_rect(struct framebuffer *fb, struct dirty_rect *out)
{
    if(!fb || !out)
        return;

    acquire(&fb->lock);
    if(fb->dirty) {
        out->top = fb->dirty_top;
        out->left = fb->dirty_left;
        out->bottom = fb->dirty_bottom;
        out->right = fb->dirty_right;
    } else {
        out->top = 0;
        out->left = 0;
        out->bottom = 0;
        out->right = 0;
    }
    release(&fb->lock);
}

/*
 * Check if framebuffer has dirty regions
 */
int
fb_is_dirty(struct framebuffer *fb)
{
    if(!fb)
        return 0;

    acquire(&fb->lock);
    int d = fb->dirty;
    release(&fb->lock);
    return d;
}

/*
 * Clear dirty flags
 */
void
fb_clear_dirty(struct framebuffer *fb)
{
    if(!fb)
        return;

    acquire(&fb->lock);
    fb->dirty = 0;
    fb->dirty_top = 0;
    fb->dirty_left = 0;
    fb->dirty_bottom = 0;
    fb->dirty_right = 0;
    fb->dirty_rect_count = 0;
    memset(fb->dirty_rects, 0, sizeof(fb->dirty_rects));
    release(&fb->lock);
}

int
fb_get_dirty_rect_count(struct framebuffer *fb)
{
    int count;

    if(!fb)
        return 0;

    acquire(&fb->lock);
    count = fb->dirty ? fb->dirty_rect_count : 0;
    release(&fb->lock);
    return count;
}

int
fb_get_dirty_rect_at(struct framebuffer *fb, int index, struct dirty_rect *out)
{
    int ok;

    if(!fb || !out)
        return -1;

    acquire(&fb->lock);
    ok = fb->dirty && index >= 0 && index < fb->dirty_rect_count;
    if(ok)
        *out = fb->dirty_rects[index];
    release(&fb->lock);

    return ok ? 0 : -1;
}

/*
 * Set a single pixel (inline for performance)
 */
void
fb_set_pixel(struct framebuffer *fb, int x, int y, uint pixel)
{
    if(!fb || x < 0 || y < 0 || x >= (int)fb->width || y >= (int)fb->height)
        return;

    uchar *pixels = (uchar *)fb->pixels;
    uint offset = y * fb->stride + x * fb->bpp;

    switch(fb->bpp) {
    case 2: {
        ushort *ptr = (ushort *)(pixels + offset);
        *ptr = (ushort)pixel;
        break;
    }
    case 4: {
        uint *ptr = (uint *)(pixels + offset);
        *ptr = pixel;
        break;
    }
    }

    fb_mark_dirty(fb, x, y, 1, 1);
}

/*
 * Get a single pixel
 */
uint
fb_get_pixel(struct framebuffer *fb, int x, int y)
{
    if(!fb || x < 0 || y < 0 || x >= (int)fb->width || y >= (int)fb->height)
        return 0;

    uchar *pixels = (uchar *)fb->pixels;
    uint offset = y * fb->stride + x * fb->bpp;

    switch(fb->bpp) {
    case 2: {
        ushort *ptr = (ushort *)(pixels + offset);
        return *ptr;
    }
    case 4: {
        uint *ptr = (uint *)(pixels + offset);
        return *ptr;
    }
    }

    return 0;
}

/*
 * Fill a rectangle with solid color
 */
void
fb_fill_rect(struct framebuffer *fb, int x, int y, uint w, uint h, uint pixel)
{
    int i, j;
    int cw;
    int ch;

    if(!fb)
        return;

    if(w == 0 || h == 0)
        return;

    cw = (int)w;
    ch = (int)h;

    if(x >= (int)fb->width || y >= (int)fb->height)
        return;

    /* Clamp to framebuffer bounds */
    if(x < 0) {
        cw += x;
        x = 0;
    }
    if(y < 0) {
        ch += y;
        y = 0;
    }
    if(x + cw > (int)fb->width)
        cw = (int)fb->width - x;
    if(y + ch > (int)fb->height)
        ch = (int)fb->height - y;

    if(cw <= 0 || ch <= 0)
        return;

    acquire(&fb->lock);

    for(j = y; j < y + ch; j++) {
        uchar *row = (uchar *)fb->pixels + j * fb->stride;
        for(i = x; i < x + cw; i++) {
            uint offset = i * fb->bpp;
            switch(fb->bpp) {
            case 2:
                *(ushort *)(row + offset) = (ushort)pixel;
                break;
            case 4:
                *(uint *)(row + offset) = pixel;
                break;
            }
        }
    }

    release(&fb->lock);

    fb_mark_dirty(fb, x, y, (uint)cw, (uint)ch);
}

/*
 * Blit a rectangular region from source to destination framebuffer
 */
void
fb_blit_rect(struct framebuffer *src, int src_x, int src_y,
             struct framebuffer *dst, int dst_x, int dst_y,
             uint w, uint h)
{
    int j;
    int cw;
    int ch;
    uchar *src_row, *dst_row;
    uint copy_len;

    if(!src || !dst)
        return;

    if(w == 0 || h == 0)
        return;

    cw = (int)w;
    ch = (int)h;

    /* Handle negative coordinates and clipping */
    if(dst_x < 0) {
        src_x -= dst_x;
        cw += dst_x;
        dst_x = 0;
    }
    if(dst_y < 0) {
        src_y -= dst_y;
        ch += dst_y;
        dst_y = 0;
    }
    if(src_x < 0) {
        cw += src_x;
        dst_x -= src_x;
        src_x = 0;
    }
    if(src_y < 0) {
        ch += src_y;
        dst_y -= src_y;
        src_y = 0;
    }

    if(src_x >= (int)src->width || src_y >= (int)src->height)
        return;
    if(dst_x >= (int)dst->width || dst_y >= (int)dst->height)
        return;

    /* Clamp width and height */
    if(src_x + cw > (int)src->width)
        cw = (int)src->width - src_x;
    if(src_y + ch > (int)src->height)
        ch = (int)src->height - src_y;
    if(dst_x + cw > (int)dst->width)
        cw = (int)dst->width - dst_x;
    if(dst_y + ch > (int)dst->height)
        ch = (int)dst->height - dst_y;

    if(cw <= 0 || ch <= 0)
        return;

    /* Only works if same pixel format */
    if(src->pixfmt != dst->pixfmt)
        return;

    copy_len = (uint)cw * src->bpp;

    acquire(&dst->lock);

    for(j = 0; j < ch; j++) {
        src_row = (uchar *)src->pixels + (src_y + j) * src->stride + src_x * src->bpp;
        dst_row = (uchar *)dst->pixels + (dst_y + j) * dst->stride + dst_x * dst->bpp;
        memmove(dst_row, src_row, copy_len);
    }

    release(&dst->lock);

    fb_mark_dirty(dst, dst_x, dst_y, (uint)cw, (uint)ch);
}

/*
 * Flush framebuffer to device (prepare for GPU transfer)
 */
void
fb_flush(struct framebuffer *fb)
{
    if(!fb)
        return;

    /* Ensure GPU sees memory updates (cache sync) */
    fb_sync_for_device(fb);
}

/*
 * Synchronize for device access (cache operations)
 */
void
fb_sync_for_device(struct framebuffer *fb)
{
    if(!fb)
        return;

    /* On x86, no explicit cache sync needed unless using Write-Back MTRRs */
    /* This is a placeholder conforming to the generic DMA API */
    dma_sync_for_device(fb->pixels, fb->size_bytes);
}

/*
 * Synchronize for CPU access after GPU modification
 */
void
fb_sync_for_cpu(struct framebuffer *fb)
{
    if(!fb)
        return;

    dma_sync_for_cpu(fb->pixels, fb->size_bytes);
}

/* TODO: Stub implementations for future use */

void *
fb_convert_buffer(void *src, uint src_fmt, void *dst, uint dst_fmt,
                  uint width, uint height)
{
    /* TODO: Implement pixel format conversion */
    cprintf("fb_convert_buffer: not implemented\n");
    return 0;
}

int
fb_convert_pixel(uint src_fmt, uint dst_fmt, uint src_pixel, uint *dst_pixel)
{
    /* TODO: Implement pixel format conversion */
    *dst_pixel = src_pixel;
    return 0;
}

uint
fb_color_to_pixel(uint pixfmt, struct color c)
{
    /* TODO: Implement color space conversion */
    return 0;
}

struct color
fb_pixel_to_color(uint pixfmt, uint pixel)
{
    struct color c = {0, 0, 0, 255};
    /* TODO: Implement color space conversion */
    return c;
}

void
fb_fill_pattern(struct framebuffer *fb, int x, int y, uint w, uint h,
                const void *pattern, uint pattern_pitch)
{
    /* TODO: Implement pattern fill */
    (void)fb; (void)x; (void)y; (void)w; (void)h;
    (void)pattern; (void)pattern_pitch;
}

void
fb_blit_clipped(struct framebuffer *src, struct dirty_rect *src_region,
                struct framebuffer *dst, int dst_x, int dst_y)
{
    /* TODO: Implement clipped blitting */
    (void)src; (void)src_region; (void)dst; (void)dst_x; (void)dst_y;
}

void
fb_blit_alpha(struct framebuffer *src, struct framebuffer *dst,
              int dst_x, int dst_y, uchar alpha)
{
    /* TODO: Implement alpha blending */
    (void)src; (void)dst; (void)dst_x; (void)dst_y; (void)alpha;
}

struct scanout_buffer *
fb_create_scanout(struct framebuffer *fb)
{
    /* TODO: Implement scanout buffer creation */
    (void)fb;
    return 0;
}

void
fb_destroy_scanout(struct scanout_buffer *sb)
{
    /* TODO: Implement scanout buffer destruction */
    (void)sb;
}

int
fb_map_user(struct framebuffer *fb)
{
    /* TODO: Implement userspace mmap */
    (void)fb;
    return -1;
}

int
fb_unmap_user(struct framebuffer *fb)
{
    /* TODO: Implement userspace unmap */
    (void)fb;
    return -1;
}

int
fb_draw_glyph(struct framebuffer *fb, const struct font *font,
              int x, int y, uint codepoint, uint fg, uint bg)
{
    /* TODO: Integrate with font rendering */
    (void)fb; (void)font; (void)x; (void)y;
    (void)codepoint; (void)fg; (void)bg;
    return -1;
}
