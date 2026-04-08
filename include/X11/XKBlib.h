/* X11/XKBlib.h - Minimal XKBlib stub for auxv6
 *
 * This provides minimal XKB declarations to allow compiling st upstream code.
 */

#ifndef _XKB_LIB_H_
#define _XKB_LIB_H_

#include "X11/Xlib.h"

typedef struct _XkbDesc *XkbDescPtr;

#define XkbKeyNameLength 4
#define XkbNumVirtualMods 16
#define XkbAllModifiersMask ((1 << 8) - 1)

typedef struct {
  unsigned char name[XkbKeyNameLength];
} XkbKeyNameRec;

typedef struct {
  int flags;
  KeySym *syms;
  int syms_rtrn;
} XkbKeyTypeRec;

typedef struct {
  XkbKeyNameRec *names;
  int nKeys;
} XkbNamesRec;

typedef struct {
  int nTypes;
  int nGroups;
  unsigned char *map;
  XkbKeyTypeRec *types;
} XkbMapRec;

typedef struct _XkbDesc {
  Display *dpy;
  unsigned short flags;
  unsigned short device_spec;
  XkbMapRec *map;
  XkbNamesRec *names;
} XkbDesc;

/* Function stubs */

static inline XkbDescPtr XkbGetKeyboard(Display *dpy, unsigned int which, unsigned int deviceSpec) {
  (void)dpy;
  (void)which;
  (void)deviceSpec;
  return 0;  /* Stub: return null */
}

static inline Status XkbGetNames(Display *dpy, unsigned int which, XkbDescPtr xkb) {
  (void)dpy;
  (void)which;
  (void)xkb;
  return 0;  /* Stub: return success */
}

static inline void XkbFreeKeyboard(XkbDescPtr xkb, unsigned int which, Bool freeMap) {
  (void)xkb;
  (void)which;
  (void)freeMap;
  /* Stub: no-op */
}

static inline KeySym XkbKeycodeToKeysym(Display *display, KeyCode code, int group, int level) {
  (void)display;
  (void)group;
  (void)level;
  /* Basic fallback: just return code as-is */
  return code;
}

static inline void XkbBell(Display *display, Window window, int percent, Atom name) {
  (void)display;
  (void)window;
  (void)percent;
  (void)name;
}

#endif
