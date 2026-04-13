#ifndef AUXV6_IMG_JPG_H
#define AUXV6_IMG_JPG_H

#include "types.h"
#include "stddef.h"
#include "auxv6/img.h"

int aux_img_jpg_decode(const uchar *src, size_t len, struct aux_img *out);

#endif
