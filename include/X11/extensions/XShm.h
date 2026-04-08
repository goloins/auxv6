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

#endif
