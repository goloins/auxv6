#ifndef _X11_EXTENSIONS_XRES_H_
#define _X11_EXTENSIONS_XRES_H_

#include <X11/Xlib.h>

typedef struct {
  long spec;
  long length;
  long *ids;
} XResClientIdSpec;

typedef struct {
  long spec;
  long length;
  unsigned char *value;
} XResClientIdValue;

Status XResQueryClientIds(Display *display, long num_specs,
                          XResClientIdSpec *client_specs,
                          long *num_ids, XResClientIdValue **client_ids);
Status XResGetClientPid(Display *display, XID resource_base,
                        long *pid_return);
void XResClientIdsDestroy(long num_ids, XResClientIdValue *client_ids);

#endif
