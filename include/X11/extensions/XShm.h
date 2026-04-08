#ifndef _X11_EXTENSIONS_XSHM_H_
#define _X11_EXTENSIONS_XSHM_H_

#include <X11/Xlib.h>

typedef struct {
  void *shmseg;
  int shmid;
  char *shmaddr;
  Bool readOnly;
} XShmSegmentInfo;

Bool XShmQueryExtension(Display *display);
XImage *XShmCreateImage(Display *display, Visual *visual, unsigned int depth,
                        int format, char *data,
                        XShmSegmentInfo *shminfo,
                        unsigned int width, unsigned int height);
Bool XShmAttach(Display *display, XShmSegmentInfo *shminfo);
Bool XShmDetach(Display *display, XShmSegmentInfo *shminfo);
Bool XShmPutImage(Display *display, Drawable d, GC gc, XImage *image,
                  int src_x, int src_y, int dst_x, int dst_y,
                  unsigned int src_width, unsigned int src_height,
                  Bool send_event);
Bool XShmGetImage(Display *display, Drawable d, XImage *image,
                  int x, int y, unsigned long plane_mask);

#endif
