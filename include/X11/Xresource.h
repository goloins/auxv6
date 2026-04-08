#ifndef _X11_XRESOURCE_H_
#define _X11_XRESOURCE_H_

#include <X11/Xlib.h>

typedef char *XrmString;
typedef XPointer XrmValuePtr;
typedef struct {
  unsigned int size;
  char *addr;
} XrmValue;
typedef struct _XrmHashBucketRec *XrmDatabase;

#endif
