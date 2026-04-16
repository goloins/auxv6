#ifndef AUXV6_IMG_PNG_H
#define AUXV6_IMG_PNG_H

#include "types.h"
#include "stddef.h"
#include "auxv6/img.h"

int aux_img_png_decode(const uchar *src, size_t len, struct aux_img *out);

#endif
