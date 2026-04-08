#ifndef _X11_EXTENSIONS_XINERAMA_H_
#define _X11_EXTENSIONS_XINERAMA_H_

#include <X11/Xlib.h>

typedef struct {
  int screen_number;
  short x_org;
  short y_org;
  short width;
  short height;
} XineramaScreenInfo;

Bool XineramaIsActive(Display *display);
XineramaScreenInfo *XineramaQueryScreens(Display *display, int *number);

#endif
