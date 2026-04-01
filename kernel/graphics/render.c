/*
 * Text rendering stubs for auxv6.
 *
 * This file intentionally provides a minimal, compile-safe implementation
 * that matches include/graphics/render.h exactly.
 */

#include "types.h"
#include "defs.h"
#include "spinlock.h"
#include "graphics/render.h"

static const uint ansi16_rgb[16] = {
    0x000000, 0xAA0000, 0x00AA00, 0xAA5500,
    0x0000AA, 0xAA00AA, 0x00AAAA, 0xAAAAAA,
    0x555555, 0xFF5555, 0x55FF55, 0xFFFF55,
    0x5555FF, 0xFF55FF, 0x55FFFF, 0xFFFFFF,
};

#define VT_MAX_WIDTH 80
#define VT_MAX_HEIGHT 25
#define VT_MAX_CELLS (VT_MAX_WIDTH * VT_MAX_HEIGHT)

static struct text_cell vt0_cells[VT_MAX_CELLS];
static uchar vt0_dirty[VT_MAX_CELLS];
static int vt0_in_use;

static uint
ansi_index_to_pixel(struct vt_surface *vts, int idx)
{
    if(!vts)
        return 0;
    if(idx < 0 || idx >= 16)
        idx = 0;
    return vts->palette[idx];
}

static void
render_cell_blocky(struct vt_surface *vts, int x, int y, struct text_cell *tc)
{
    int px;
    int py;
    uint bg;
    uint fg;

    if(!vts || !vts->fb || !tc)
        return;

    px = vts->fb_x + x * 8;
    py = vts->fb_y + y * 16;
    bg = ansi_index_to_pixel(vts, tc->bg_color & 0x0F);
    fg = ansi_index_to_pixel(vts, tc->fg_color & 0x0F);

    fb_fill_rect(vts->fb, px, py, 8, 16, bg);

    if(tc->codepoint != ' ' && tc->codepoint != 0) {
        fb_fill_rect(vts->fb, px + 2, py + 3, 4, 10, fg);
    }
}

static int
cell_index(struct vt_surface *vts, int x, int y)
{
    if(!vts)
        return -1;
    if(x < 0 || y < 0)
        return -1;
    if(x >= (int)vts->width || y >= (int)vts->height)
        return -1;
    return y * (int)vts->width + x;
}

struct render_context *
render_context_create(struct framebuffer *fb, struct font *font)
{
    struct render_context *ctx;

    ctx = (struct render_context *)kalloc();
    if(!ctx)
        return 0;

    memset(ctx, 0, sizeof(*ctx));
    ctx->fb = fb;
    ctx->font = font;
    return ctx;
}

void
render_context_destroy(struct render_context *ctx)
{
    if(!ctx)
        return;
    kfree((char *)ctx);
}

void
render_context_set_palette(struct render_context *ctx,
                           struct color_palette *palette)
{
    if(!ctx)
        return;
    ctx->palette = palette;
}

void
render_context_set_clip(struct render_context *ctx,
                        int x, int y, uint w, uint h)
{
    if(!ctx)
        return;
    ctx->clip_x = x;
    ctx->clip_y = y;
    ctx->clip_width = (int)w;
    ctx->clip_height = (int)h;
}

struct vt_surface *
vt_surface_create(uint width, uint height, struct render_context *ctx)
{
    struct vt_surface *vts;
    uint n;

    if(width == 0 || height == 0)
        return 0;

    if(width > VT_MAX_WIDTH || height > VT_MAX_HEIGHT)
        return 0;

    if(vt0_in_use)
        return 0;

    vts = (struct vt_surface *)kalloc();
    if(!vts)
        return 0;

    memset(vts, 0, sizeof(*vts));
    initlock(&vts->lock, "vt_surface");

    vts->width = width;
    vts->height = height;
    vts->cursor_visible = 1;
    vts->flags = VT_FLAG_CURSOR_VISIBLE;
    vts->fg_color = 7;
    vts->bg_color = 0;
    n = width * height;
    vts->cells = vt0_cells;
    vts->dirty = vt0_dirty;
    memset(vts->cells, 0, n * sizeof(struct text_cell));
    memset(vts->dirty, 1, n);
    vts->any_dirty = 1;
    vt0_in_use = 1;

    if(ctx) {
        vts->ctx = *ctx;
        vts->fb = ctx->fb;
        vts->font = ctx->font;
    }

    memmove(vts->palette, ansi16_rgb, sizeof(ansi16_rgb));
    return vts;
}

void
vt_surface_destroy(struct vt_surface *vts)
{
    uint n;

    if(!vts)
        return;

    n = vts->width * vts->height;
    if(vts->cells == vt0_cells)
        memset(vts->cells, 0, n * sizeof(struct text_cell));
    if(vts->dirty == vt0_dirty)
        memset(vts->dirty, 0, n);
    vt0_in_use = 0;

    if(vts->scrollback)
        kfree((char *)vts->scrollback);
    if(vts->dirty_cells)
        kfree((char *)vts->dirty_cells);
    kfree((char *)vts);
}

int
vt_surface_resize(struct vt_surface *vts, uint new_width, uint new_height)
{
    uint n;

    if(!vts || new_width == 0 || new_height == 0)
        return -1;
    if(new_width > VT_MAX_WIDTH || new_height > VT_MAX_HEIGHT)
        return -1;

    acquire(&vts->lock);
    vts->width = new_width;
    vts->height = new_height;
    n = new_width * new_height;
    memset(vts->cells, 0, n * sizeof(struct text_cell));
    memset(vts->dirty, 1, n);
    vts->any_dirty = 1;
    release(&vts->lock);
    return 0;
}

void
vt_set_cell(struct vt_surface *vts, int x, int y, struct text_cell *cell)
{
    int idx;

    if(!vts || !cell || !vts->cells)
        return;

    idx = cell_index(vts, x, y);
    if(idx < 0)
        return;

    acquire(&vts->lock);
    vts->cells[idx] = *cell;
    if(vts->dirty)
        vts->dirty[idx] = 1;
    vts->any_dirty = 1;
    release(&vts->lock);
}

void
vt_get_cell(struct vt_surface *vts, int x, int y, struct text_cell *cell_out)
{
    int idx;

    if(cell_out)
        memset(cell_out, 0, sizeof(*cell_out));
    if(!vts || !cell_out || !vts->cells)
        return;

    idx = cell_index(vts, x, y);
    if(idx < 0)
        return;

    acquire(&vts->lock);
    *cell_out = vts->cells[idx];
    release(&vts->lock);
}

void
vt_clear_rect(struct vt_surface *vts, int x, int y, uint w, uint h, uint bg_color)
{
    (void)x;
    (void)y;
    (void)w;
    (void)h;
    if(!vts)
        return;
    acquire(&vts->lock);
    vts->bg_color = (int)(bg_color & 0xF);
    vts->any_dirty = 1;
    release(&vts->lock);
}

void
vt_set_cursor(struct vt_surface *vts, int x, int y)
{
    if(!vts)
        return;
    acquire(&vts->lock);
    vts->cursor_x = x;
    vts->cursor_y = y;
    vts->any_dirty = 1;
    release(&vts->lock);
}

void
vt_get_cursor(struct vt_surface *vts, int *x_out, int *y_out)
{
    if(!vts)
        return;
    acquire(&vts->lock);
    if(x_out)
        *x_out = vts->cursor_x;
    if(y_out)
        *y_out = vts->cursor_y;
    release(&vts->lock);
}

void
vt_show_cursor(struct vt_surface *vts, int visible)
{
    if(!vts)
        return;
    acquire(&vts->lock);
    vts->cursor_visible = visible ? 1 : 0;
    if(vts->cursor_visible)
        vts->flags |= VT_FLAG_CURSOR_VISIBLE;
    else
        vts->flags &= ~VT_FLAG_CURSOR_VISIBLE;
    vts->any_dirty = 1;
    release(&vts->lock);
}

void
vt_scroll_up(struct vt_surface *vts, int lines)
{
    (void)lines;
    if(!vts)
        return;
    acquire(&vts->lock);
    vts->any_dirty = 1;
    release(&vts->lock);
}

void
vt_scroll_down(struct vt_surface *vts, int lines)
{
    (void)lines;
    if(!vts)
        return;
    acquire(&vts->lock);
    vts->any_dirty = 1;
    release(&vts->lock);
}

void
vt_mark_cell_dirty(struct vt_surface *vts, int x, int y)
{
    int idx;

    if(!vts)
        return;

    idx = cell_index(vts, x, y);
    if(idx < 0)
        return;

    acquire(&vts->lock);
    if(vts->dirty)
        vts->dirty[idx] = 1;
    vts->any_dirty = 1;
    release(&vts->lock);
}

void
vt_mark_dirty(struct vt_surface *vts, int x, int y, uint w, uint h)
{
    int ix;
    int iy;

    if(!vts)
        return;

    acquire(&vts->lock);
    for(iy = y; iy < y + (int)h; iy++) {
        for(ix = x; ix < x + (int)w; ix++) {
            int idx = cell_index(vts, ix, iy);
            if(idx >= 0 && vts->dirty)
                vts->dirty[idx] = 1;
        }
    }
    vts->any_dirty = 1;
    release(&vts->lock);
}

void
vt_mark_all_dirty(struct vt_surface *vts)
{
    uint n;

    if(!vts)
        return;
    acquire(&vts->lock);
    n = vts->width * vts->height;
    if(vts->dirty)
        memset(vts->dirty, 1, n);
    vts->any_dirty = 1;
    release(&vts->lock);
}

int
vt_any_dirty(struct vt_surface *vts)
{
    int v;
    if(!vts)
        return 0;
    acquire(&vts->lock);
    v = vts->any_dirty;
    release(&vts->lock);
    return v;
}

void
vt_clear_dirty(struct vt_surface *vts)
{
    uint n;

    if(!vts)
        return;
    acquire(&vts->lock);
    n = vts->width * vts->height;
    if(vts->dirty)
        memset(vts->dirty, 0, n);
    vts->any_dirty = 0;
    release(&vts->lock);
}

int
vt_render(struct vt_surface *vts)
{
    uint x;
    uint y;
    int idx;

    if(!vts)
        return 0;

    if(!vts->fb || !vts->cells)
        return -1;

    acquire(&vts->lock);
    for(y = 0; y < vts->height; y++) {
        for(x = 0; x < vts->width; x++) {
            idx = (int)(y * vts->width + x);
            render_cell_blocky(vts, (int)x, (int)y, &vts->cells[idx]);
            if(vts->dirty)
                vts->dirty[idx] = 0;
        }
    }
    vts->any_dirty = 0;
    release(&vts->lock);

    vt_clear_dirty(vts);
    return 0;
}

int
vt_render_dirty(struct vt_surface *vts)
{
    uint x;
    uint y;
    int idx;

    if(!vts)
        return 0;
    if(!vts->fb || !vts->cells)
        return -1;

    acquire(&vts->lock);
    if(!vts->any_dirty) {
        release(&vts->lock);
        return 0;
    }

    for(y = 0; y < vts->height; y++) {
        for(x = 0; x < vts->width; x++) {
            idx = (int)(y * vts->width + x);
            if(vts->dirty && !vts->dirty[idx])
                continue;
            render_cell_blocky(vts, (int)x, (int)y, &vts->cells[idx]);
            if(vts->dirty)
                vts->dirty[idx] = 0;
        }
    }

    vts->any_dirty = 0;
    release(&vts->lock);
    return 0;
}

int
vt_render_cursor(struct vt_surface *vts)
{
    (void)vts;
    return 0;
}

struct color_palette *
color_palette_create(void)
{
    struct color_palette *pal;
    pal = (struct color_palette *)kalloc();
    if(!pal)
        return 0;
    memset(pal, 0, sizeof(*pal));
    return pal;
}

void
color_palette_destroy(struct color_palette *pal)
{
    if(!pal)
        return;
    kfree((char *)pal);
}

void
color_palette_set_color(struct color_palette *pal, int index,
                        uchar r, uchar g, uchar b)
{
    uint rgb;
    if(!pal || index < 0 || index >= 256)
        return;
    rgb = ((uint)r << 16) | ((uint)g << 8) | (uint)b;
    pal->colors[index] = rgb;
}

uint
color_palette_lookup(struct color_palette *pal, int index)
{
    if(!pal || index < 0 || index >= 256)
        return 0;
    return pal->colors[index];
}

struct color_palette *
color_palette_ansi_16(void)
{
    struct color_palette *pal;
    int i;

    pal = color_palette_create();
    if(!pal)
        return 0;

    for(i = 0; i < 16; i++)
        pal->colors[i] = ansi16_rgb[i];
    return pal;
}

struct color_palette *
color_palette_xterm_256(void)
{
    return color_palette_ansi_16();
}

void
ansi_color_to_rgb(int color_index, uchar *r_out, uchar *g_out, uchar *b_out)
{
    uint rgb;

    if(color_index < 0 || color_index >= 16)
        color_index = 0;
    rgb = ansi16_rgb[color_index];

    if(r_out)
        *r_out = (uchar)((rgb >> 16) & 0xFF);
    if(g_out)
        *g_out = (uchar)(rgb & 0xFF);
    if(b_out)
        *b_out = (uchar)((rgb >> 8) & 0xFF);
}
