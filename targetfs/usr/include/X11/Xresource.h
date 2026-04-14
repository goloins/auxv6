#ifndef _X11_XRESOURCE_H_
#define _X11_XRESOURCE_H_

#include <X11/Xlib.h>

typedef char *XrmString;
typedef XPointer XrmValuePtr;
typedef char *XrmRepresentation;
typedef struct {
  unsigned int size;
  char *addr;
} XrmValue;
typedef struct _XrmHashBucketRec *XrmDatabase;

void XrmInitialize(void);
char *XResourceManagerString(Display *display);
XrmDatabase XrmGetStringDatabase(const char *data);
void XrmDestroyDatabase(XrmDatabase database);
Bool XrmGetResource(XrmDatabase database, const char *str_name,
                    const char *str_class, char **str_type_return,
                    XrmValue *value_return);
void XrmPutStringResource(XrmDatabase *database, const char *specifier,
                          const char *value);
void XrmMergeDatabases(XrmDatabase source_db, XrmDatabase *target_db);

#endif
