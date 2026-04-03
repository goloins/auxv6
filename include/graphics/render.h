/*
 * Text Rendering Pipeline for auxv6
 *
 * High-level text rendering with dirty tracking and per-cell updates.
 * Integrates font rasterization with framebuffer bit output.
 */

#ifndef _GRAPHICS_RENDER_H_
#define _GRAPHICS_RENDER_H_

#include "types.h"
#include "graphics/framebuffer.h"
#include "graphics/font.h"

/* Color palette (standard ANSI 256-color) */
struct color_palette {
    uint colors[256];        /* Device pixel format appropriate to framebuffer */
};

/* Rendering context for a drawable surface */
struct render_context {
    struct framebuffer *fb;
    struct font *font;
    struct color_palette *palette;
    
    /* Clipping region */
    int clip_x, clip_y;
    int clip_width, clip_height;
    
    int text_baseline_offset;
};

/* VT surface - terminal-like drawable */
#define VT_FLAG_CURSOR_VISIBLE 1
#define VT_SURFACE_MAX_WIDTH 160
#define VT_SURFACE_MAX_HEIGHT 64

struct vt_surface {
    /* Content */
    struct text_cell *cells;         /* width × height */
    uint width, height;
    
    /* Cursor */
    int cursor_x, cursor_y;
    int cursor_visible;
    int flags;                       /* VT_FLAG_* */
    
    /* Colors - per-surface default */
    int fg_color, bg_color;          /* ANSI color indices 0-15 */
    
    /* Palette */
    uint palette[16];                /* 16-color ANSI palette */
    
    /* Font */
    struct font *font;
    
    /* Scrollback */
    struct text_cell *scrollback;
    uint scrollback_size;
    uint scrollback_lines;
    uint scroll_offset;
    
    /* Rendering */
    struct render_context ctx;
    
    /* Dirty tracking - per-cell */
    uchar *dirty;                    /* 1 byte per cell */
    uchar *dirty_cells;              /* bitmap of dirty cells */
    int any_dirty;
    
    /* Framebuffer reference */
    struct framebuffer *fb;
    
    /* Binding to framebuffer region */
    int fb_x, fb_y;                  /* offset in framebuffer */
    
    /* State */
    struct spinlock lock;
};

/* Function prototypes */

/* Render context management */
struct render_context *render_context_create(struct framebuffer *fb,
                                             struct font *font);
void render_context_destroy(struct render_context *ctx);
void render_context_set_palette(struct render_context *ctx,
                                struct color_palette *palette);
void render_context_set_clip(struct render_context *ctx,
                             int x, int y, uint w, uint h);
void render_pick_cell_metrics(struct font *font,
                              uint pixel_width, uint pixel_height,
                              uint cols, uint rows,
                              int *cell_w_out, int *cell_h_out);

/* VT Surface management */
struct vt_surface *vt_surface_create(uint width, uint height,
                                     struct render_context *ctx);
void vt_surface_destroy(struct vt_surface *vts);
int vt_surface_resize(struct vt_surface *vts, uint new_width, uint new_height);

/* Content manipulation */
void vt_set_cell(struct vt_surface *vts, int x, int y,
                 struct text_cell *cell);
void vt_get_cell(struct vt_surface *vts, int x, int y,
                 struct text_cell *cell_out);
void vt_clear_rect(struct vt_surface *vts, int x, int y,
                   uint w, uint h, uint bg_color);

/* Cursor manipulation */
void vt_set_cursor(struct vt_surface *vts, int x, int y);
void vt_get_cursor(struct vt_surface *vts, int *x_out, int *y_out);
void vt_show_cursor(struct vt_surface *vts, int visible);

/* Scrolling */
void vt_scroll_up(struct vt_surface *vts, int lines);
void vt_scroll_down(struct vt_surface *vts, int lines);

/* Dirty tracking */
void vt_mark_cell_dirty(struct vt_surface *vts, int x, int y);
void vt_mark_dirty(struct vt_surface *vts, int x, int y, uint w, uint h);
void vt_mark_all_dirty(struct vt_surface *vts);
int vt_any_dirty(struct vt_surface *vts);
void vt_clear_dirty(struct vt_surface *vts);

/* Rendering - convert surface to pixels */
int vt_render(struct vt_surface *vts);
int vt_render_dirty(struct vt_surface *vts);
int vt_render_cursor(struct vt_surface *vts);

/* Color palette utilities */
struct color_palette *color_palette_create(void);
void color_palette_destroy(struct color_palette *pal);
void color_palette_set_color(struct color_palette *pal, int index,
                             uchar r, uchar g, uchar b);
uint color_palette_lookup(struct color_palette *pal, int index);

/* Standard palettes */
struct color_palette *color_palette_ansi_16(void);
struct color_palette *color_palette_xterm_256(void);

/* Utility - convert ANSI color to RGB */
void ansi_color_to_rgb(int color_index, uchar *r_out, uchar *g_out, uchar *b_out);

#endif /* _GRAPHICS_RENDER_H_ */
