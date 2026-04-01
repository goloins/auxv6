/*
 * Font and Text Rendering for auxv6
 *
 * Bitmap font rasterization for terminal-like text rendering.
 * Supports Unicode codepoints and terminal attributes (bold, color, etc).
 *
 * Design: Stateless glyph rendering + glyph cache for performance.
 */

#ifndef _GRAPHICS_FONT_H_
#define _GRAPHICS_FONT_H_

#include "types.h"

/* Glyph bitmap structure */
struct glyph {
    uint codepoint;
    int width;
    int height;
    int advance_x;           /* pixels to advance after glyph */
    int advance_y;
    int bearing_x;           /* left offset from baseline */
    int bearing_y;           /* top offset from baseline */
    const uchar *bitmap;     /* 1bpp or 8bpp pixel data */
    int bitmap_size;
};

/* Font structure */
struct font {
    char name[32];
    int size;                /* height in pixels */
    int ascent;
    int descent;
    int linegap;
    int is_monospace;
    uint num_glyphs;
    
    /* Glyph lookup */
    struct glyph *glyphs;
    struct spinlock lock;
    
    /* Cache (optional, for performance) */
    void *cache;
    int cache_size;
};

/* Rendering attributes for text display */
#define TEXT_ATTR_BOLD       0x01
#define TEXT_ATTR_DIM        0x02
#define TEXT_ATTR_ITALIC     0x04
#define TEXT_ATTR_UNDERLINE  0x08
#define TEXT_ATTR_BLINK      0x10
#define TEXT_ATTR_REVERSE    0x20
#define TEXT_ATTR_HIDDEN     0x40
#define TEXT_ATTR_STRIKEOUT  0x80

/* Terminal cell for text rendering */
struct text_cell {
    uint codepoint;
    uchar attr;              /* TEXT_ATTR_* flags */
    uchar fg_color;          /* ANSI color 0-255 */
    uchar bg_color;
    int width;               /* character width (1 or 2 for wide chars) */
};

/* Function prototypes */

/* Font management */
struct font *font_load(const char *name, int size);
struct font *font_builtin_default(void);
struct font *font_builtin_mono(void);
void font_free(struct font *font);

/* Glyph operations */
const struct glyph *font_get_glyph(struct font *font, uint codepoint);
int font_has_glyph(struct font *font, uint codepoint);
uint font_substitute_glyph(struct font *font, uint codepoint);

/* Rendering - typically called from framebuffer.c */
void font_render_glyph_1bpp(struct font *font, const struct glyph *glyph,
                            uchar *pixels, int pitch,
                            uint fg, uint bg);
void font_render_glyph_8bpp(struct font *font, const struct glyph *glyph,
                            uchar *pixels, int pitch,
                            uint fg, uint bg);

/* Metrics */
int font_text_width(struct font *font, const uint *codepoints, int count);
int font_text_height(struct font *font);
int font_char_width(struct font *font, uint codepoint);

#endif /* _GRAPHICS_FONT_H_ */
