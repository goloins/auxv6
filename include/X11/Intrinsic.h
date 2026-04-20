#ifndef _X11_INTRINSIC_H_
#define _X11_INTRINSIC_H_

#include <X11/Xos.h>
#include <X11/Xlib.h>
#include <X11/Xresource.h>

typedef Bool Boolean;
typedef char *String;
typedef unsigned int Cardinal;
typedef void *XtPointer;

typedef struct {
  unsigned int size;
  char *addr;
} XtArgValCompat;

#ifndef XtNumber
#define XtNumber(arr) (sizeof(arr) / sizeof((arr)[0]))
#endif

#endif
