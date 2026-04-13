#include "types.h"
#include "auxv6/user.h"
#include "socket.h"
#include "fcntl.h"
#include "sys/ioctl.h"
#include "graphics/drm_ioctls.h"
#include "auxv6/img.h"
#include "stdio.h"
#include "string.h"

#define WALLPAPER_FBIOGET_VSCREENINFO 0x4600
#define WALLPAPER_FBIOGET_FSCREENINFO 0x4602
#define WALLPAPER_X6_PORT 6006

struct fb_ctx {
  int fd;
  int width;
  int height;
  int stride;
};

static int
wallpaper_read_line(int fd, char *buf, int bufsz)
{
  int pos;

  if(fd < 0 || !buf || bufsz <= 1)
    return -1;

  pos = 0;
  while(pos < bufsz - 1) {
    char ch;
    int n;
    n = recv(fd, &ch, 1);
    if(n <= 0)
      return -1;
    if(ch == '\n' || ch == '\r') {
      buf[pos] = 0;
      return 0;
    }
    buf[pos++] = ch;
  }
  buf[pos] = 0;
  return 0;
}

static int
wallpaper_parse_dim(const char *line, const char *key, int def)
{
  const char *p;
  int v;

  if(!line || !key)
    return def;
  p = strstr(line, key);
  if(!p)
    return def;
  p += strlen(key);
  v = atoi(p);
  return v > 0 ? v : def;
}

static int
wallpaper_try_x6_color(uint rgb)
{
  int fd;
  struct sockaddr_in dst;
  char line[256];
  int w;
  int h;

  fd = socket(AF_INET, SOCK_STREAM, 0);
  if(fd < 0)
    return -1;

  memset(&dst, 0, sizeof(dst));
  dst.sin_family = AF_INET;
  dst.sin_port = (ushort)WALLPAPER_X6_PORT;
  dst.sin_addr.s_addr = INADDR_LOOPBACK;
  if(connect(fd, &dst, sizeof(dst)) < 0) {
    close(fd);
    return -1;
  }

  if(wallpaper_read_line(fd, line, sizeof(line)) < 0) {
    close(fd);
    return -1;
  }
  send(fd, "HELLO x6/1\n", 11);
  if(wallpaper_read_line(fd, line, sizeof(line)) < 0) {
    close(fd);
    return -1;
  }
  if(strncmp(line, "OK proto=", 9) != 0) {
    close(fd);
    return -1;
  }

  w = wallpaper_parse_dim(line, "width=", 1024);
  h = wallpaper_parse_dim(line, "height=", 768);
  snprintf(line, sizeof(line), "DRAW_RECT 1 0 0 %d %d %u\n", w, h, rgb & 0x00ffffffU);
  if(send(fd, line, strlen(line)) < 0) {
    close(fd);
    return -1;
  }

  /* Draw requests are fire-and-forget on current x6. Older servers replied
   * with "OK draw"; we no longer block waiting for that ack so startup scripts
   * cannot stall between wallpaper and WM launch. */
  send(fd, "DETACH\n", 7);
  close(fd);
  return 0;
}

static int
parse_hex_byte(char a, char b)
{
  int hi, lo;

  if(a >= '0' && a <= '9') hi = a - '0';
  else if(a >= 'a' && a <= 'f') hi = 10 + (a - 'a');
  else if(a >= 'A' && a <= 'F') hi = 10 + (a - 'A');
  else return -1;

  if(b >= '0' && b <= '9') lo = b - '0';
  else if(b >= 'a' && b <= 'f') lo = 10 + (b - 'a');
  else if(b >= 'A' && b <= 'F') lo = 10 + (b - 'A');
  else return -1;

  return (hi << 4) | lo;
}

static int
parse_color(const char *s, uint *rgb)
{
  int r, g, b;

  if(!s || !rgb)
    return -1;

  if(s[0] == '#')
    s++;

  if(strlen(s) != 6)
    return -1;

  r = parse_hex_byte(s[0], s[1]);
  g = parse_hex_byte(s[2], s[3]);
  b = parse_hex_byte(s[4], s[5]);
  if(r < 0 || g < 0 || b < 0)
    return -1;

  *rgb = ((uint)r << 16) | ((uint)g << 8) | (uint)b;
  return 0;
}

static int
fb_open(struct fb_ctx *fb)
{
  struct fb_var_screeninfo vinfo;
  struct fb_fix_screeninfo finfo;

  fb->fd = open("/dev/fb0", O_RDWR);
  if(fb->fd < 0)
    return -1;

  if(ioctl(fb->fd, WALLPAPER_FBIOGET_VSCREENINFO, &vinfo) < 0)
    return -1;
  if(ioctl(fb->fd, WALLPAPER_FBIOGET_FSCREENINFO, &finfo) < 0)
    return -1;

  if(vinfo.bits_per_pixel != 32)
    return -1;

  fb->width = (int)vinfo.xres;
  fb->height = (int)vinfo.yres;
  fb->stride = (int)finfo.line_length;
  return 0;
}

static void
fb_close(struct fb_ctx *fb)
{
  if(fb->fd >= 0)
    close(fb->fd);
  fb->fd = -1;
}

static int
fb_write_rows(struct fb_ctx *fb, uint *row, int y0, int y1)
{
  int y;

  for(y = y0; y < y1; y++) {
    uint64_t off = (uint64_t)y * (uint64_t)fb->stride;
    if(lseek(fb->fd, (off_t)off, SEEK_SET) < 0)
      return -1;
    if(write(fb->fd, row, fb->width * (int)sizeof(uint)) < 0)
      return -1;
  }
  return 0;
}

static int
set_color(struct fb_ctx *fb, uint rgb)
{
  uint *row;
  int i;
  int rv;

  row = (uint*)malloc((size_t)fb->width * sizeof(uint));
  if(!row)
    return -1;

  for(i = 0; i < fb->width; i++)
    row[i] = rgb & 0x00ffffffU;

  rv = fb_write_rows(fb, row, 0, fb->height);
  free(row);
  return rv;
}

static int
set_image(struct fb_ctx *fb, const struct aux_img *img)
{
  uint *row;
  int y;

  row = (uint*)malloc((size_t)fb->width * sizeof(uint));
  if(!row)
    return -1;

  for(y = 0; y < fb->height; y++) {
    int sy = (img->height > 0) ? (y * img->height / fb->height) : 0;
    int x;

    if(sy < 0) sy = 0;
    if(sy >= img->height) sy = img->height - 1;

    for(x = 0; x < fb->width; x++) {
      int sx = (img->width > 0) ? (x * img->width / fb->width) : 0;
      int si;
      uchar r, g, b;

      if(sx < 0) sx = 0;
      if(sx >= img->width) sx = img->width - 1;

      si = sy * img->stride + sx * 4;
      r = img->pixels[si + 0];
      g = img->pixels[si + 1];
      b = img->pixels[si + 2];
      row[x] = ((uint)r << 16) | ((uint)g << 8) | (uint)b;
    }

    {
      uint64_t off = (uint64_t)y * (uint64_t)fb->stride;
      if(lseek(fb->fd, (off_t)off, SEEK_SET) < 0) {
        free(row);
        return -1;
      }
      if(write(fb->fd, row, fb->width * (int)sizeof(uint)) < 0) {
        free(row);
        return -1;
      }
    }
  }

  free(row);
  return 0;
}

static void
usage(void)
{
  dprintf(2, "usage: wallpaper [-c #RRGGBB] [image-path]\n");
  dprintf(2, "       wallpaper [#RRGGBB]\n");
  exit(1);
}

int
main(int argc, char **argv)
{
  struct fb_ctx fb;
  uint color;
  struct aux_img img;
  const char *img_path;
  const char *color_arg;

  img_path = 0;
  color_arg = 0;

  if(argc == 1) {
    color_arg = "000000";
  } else if(argc == 2) {
    if(strcmp(argv[1], "-c") == 0 || strcmp(argv[1], "--color") == 0)
      usage();
    if(parse_color(argv[1], &color) == 0)
      color_arg = argv[1];
    else
      img_path = argv[1];
  } else if(argc == 3) {
    if(strcmp(argv[1], "-c") != 0 && strcmp(argv[1], "--color") != 0)
      usage();
    color_arg = argv[2];
  } else {
    usage();
  }

  fb.fd = -1;

  if(color_arg) {
    if(parse_color(color_arg, &color) < 0) {
      dprintf(2, "wallpaper: invalid color '%s'\n", color_arg);
      return 1;
    }
    if(wallpaper_try_x6_color(color) == 0)
      return 0;
    if(fb_open(&fb) < 0) {
      dprintf(2, "wallpaper: /dev/fb0 unavailable\n");
      return 1;
    }
    if(set_color(&fb, color) < 0) {
      dprintf(2, "wallpaper: failed to set color\n");
      fb_close(&fb);
      return 1;
    }
    fb_close(&fb);
    return 0;
  }

  if(fb_open(&fb) < 0) {
    dprintf(2, "wallpaper: /dev/fb0 unavailable\n");
    return 1;
  }

  aux_img_init(&img);
  if(aux_img_decode_file(img_path, &img) < 0) {
    dprintf(2, "wallpaper: unsupported or invalid image\n");
    fb_close(&fb);
    return 1;
  }

  if(set_image(&fb, &img) < 0) {
    dprintf(2, "wallpaper: failed to draw image\n");
    aux_img_free(&img);
    fb_close(&fb);
    return 1;
  }

  aux_img_free(&img);
  fb_close(&fb);
  return 0;
}
