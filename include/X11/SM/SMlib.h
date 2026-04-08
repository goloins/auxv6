#ifndef _X11_SM_SMLIB_H_
#define _X11_SM_SMLIB_H_

#include <X11/Xlib.h>

typedef struct _SmcConnRec *SmcConn;
typedef struct _SmPointerStruct *SmPointer;

typedef struct {
  char *name;
  char *type;
  int num_vals;
  void *vals;
} SmProp;

typedef struct {
  char *name;
  int length;
  SmPointer value;
} SmPropValue;

#endif
