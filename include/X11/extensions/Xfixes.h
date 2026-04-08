#ifndef _X11_EXTENSIONS_XFIXES_H_
#define _X11_EXTENSIONS_XFIXES_H_

#include <X11/Xlib.h>

typedef XID XserverRegion;

typedef struct {
  int x;
  int y;
  unsigned int width;
  unsigned int height;
} XFixesCursorImage;

Bool XFixesQueryExtension(Display *display, int *event_base_return,
                          int *error_base_return);

#endif
