#ifndef _X11_EXTENSIONS_XSHM_H_
#define _X11_EXTENSIONS_XSHM_H_

#include <X11/Xlib.h>

typedef struct {
  void *shmseg;
  int shmid;
  char *shmaddr;
  Bool readOnly;
} XShmSegmentInfo;

#define ShmCompletion 0

typedef struct {
  int type;
  unsigned long serial;
  Bool send_event;
  Display *display;
  Drawable drawable;
  int major_code;
  int minor_code;
  void *shmseg;
  unsigned long offset;
} XShmCompletionEvent;

Bool XShmQueryExtension(Display *display);
int XShmGetEventBase(Display *display);
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
