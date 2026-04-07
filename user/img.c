#include "types.h"
#include "auxv6/user.h"
#include "fcntl.h"
#include "stat.h"
#include "auxv6/img.h"
#include "auxv6/img_png.h"
#include "auxv6/img_jpg.h"

void
aux_img_init(struct aux_img *img)
{
  if(!img)
    return;
  img->width = 0;
  img->height = 0;
  img->stride = 0;
  img->format = 0;
  img->pixels = 0;
}

void
aux_img_free(struct aux_img *img)
{
  if(!img)
    return;
  if(img->pixels)
    free(img->pixels);
  aux_img_init(img);
}

static int
img_has_prefix(const uchar *src, size_t len, const uchar *pfx, size_t pfx_len)
{
  size_t i;

  if(len < pfx_len)
    return 0;
  for(i = 0; i < pfx_len; i++)
    if(src[i] != pfx[i])
      return 0;
  return 1;
}

int
aux_img_decode_memory(const uchar *src, size_t len, struct aux_img *out)
{
  if(!src || !out)
    return -1;

  if(img_has_prefix(src, len, (const uchar*)"\x89PNG\r\n\x1a\n", 8))
    return aux_img_png_decode(src, len, out);

  if(len >= 3 && src[0] == 0xff && src[1] == 0xd8 && src[2] == 0xff)
    return aux_img_jpg_decode(src, len, out);

  return -1;
}

int
aux_img_decode_file(const char *path, struct aux_img *out)
{
  struct stat st;
  int fd;
  uchar *buf;
  int n;
  int rv;

  if(!path || !out)
    return -1;

  if(stat(path, &st) < 0)
    return -1;
  if(st.size <= 0)
    return -1;

  buf = (uchar*)malloc((size_t)st.size);
  if(!buf)
    return -1;

  fd = open(path, O_RDONLY);
  if(fd < 0) {
    free(buf);
    return -1;
  }

  n = read(fd, buf, st.size);
  close(fd);
  if(n != st.size) {
    free(buf);
    return -1;
  }

  rv = aux_img_decode_memory(buf, (size_t)n, out);
  free(buf);
  return rv;
}
