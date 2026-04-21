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

typedef enum {
  XrmoptionNoArg,
  XrmoptionIsArg,
  XrmoptionStickyArg,
  XrmoptionSepArg,
  XrmoptionResArg,
  XrmoptionSkipArg,
  XrmoptionSkipLine,
  XrmoptionSkipNArgs
} XrmOptionKind;

typedef struct {
  char *option;
  char *specifier;
  XrmOptionKind argKind;
  XPointer value;
} XrmOptionDescRec, *XrmOptionDescList;

void XrmInitialize(void);
char *XResourceManagerString(Display *display);
XrmDatabase XrmGetStringDatabase(const char *data);
void XrmDestroyDatabase(XrmDatabase database);
Bool XrmGetResource(XrmDatabase database, const char *str_name,
                    const char *str_class, char **str_type_return,
                    XrmValue *value_return);
void XrmPutStringResource(XrmDatabase *database, const char *specifier,
                          const char *value);
void XrmCombineDatabase(XrmDatabase source_db, XrmDatabase *target_db,
                        Bool override);
void XrmMergeDatabases(XrmDatabase source_db, XrmDatabase *target_db);
void XrmParseCommand(XrmDatabase *database, XrmOptionDescList table,
                     int table_count, const char *name,
                     int *argc_in_out, char **argv_in_out);

#endif
