#ifndef _GRAPHICS_USER_FONT_H_
#define _GRAPHICS_USER_FONT_H_

#include "types.h"

struct user_glyph {
  uint codepoint;
  int width;
  int height;
  int advance_x;
  int advance_y;
  int bearing_x;
  int bearing_y;
  const uchar *bitmap;
  int bitmap_size;
};

struct user_font {
  char name[32];
  int size;
  int ascent;
  int descent;
  int linegap;
  int is_monospace;
  uint num_glyphs;
  const struct user_glyph *glyphs;
};

const struct user_font *user_font_builtin_montecarlo(void);
const struct user_glyph *user_font_get_glyph(const struct user_font *font, uint codepoint);
int user_font_char_width(const struct user_font *font, uint codepoint);
int user_font_text_width(const struct user_font *font, const char *text, int len);

#endif
