#ifndef AUXV6_IMG_H
#define AUXV6_IMG_H

#include "types.h"
#include "stddef.h"

#define AUX_IMG_FMT_RGBA8 1

struct aux_img {
  int width;
  int height;
  int stride;
  int format;
  uchar *pixels;
};

void aux_img_init(struct aux_img *img);
void aux_img_free(struct aux_img *img);
int aux_img_decode_memory(const uchar *src, size_t len, struct aux_img *out);
int aux_img_decode_file(const char *path, struct aux_img *out);

#endif
