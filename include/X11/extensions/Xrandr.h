#ifndef _X11_EXTENSIONS_XRANDR_H_
#define _X11_EXTENSIONS_XRANDR_H_

#include <X11/Xlib.h>

typedef XID RROutput;
typedef XID RRCrtc;
typedef XID RRMode;

typedef struct {
  Time timestamp;
  Time configTimestamp;
  int ncrtc;
  RRCrtc *crtcs;
  int noutput;
  RROutput *outputs;
  int nmode;
  RRMode *modes;
} XRRScreenResources;

typedef struct {
  Time timestamp;
  RRCrtc crtc;
  char *name;
  int nameLen;
  unsigned long mm_width;
  unsigned long mm_height;
  int connection;
  int subpixel_order;
  int ncrtc;
  RRCrtc *crtcs;
  int nclone;
  RROutput *clones;
  int nmode;
  int npreferred;
  RRMode *modes;
} XRROutputInfo;

typedef struct {
  Time timestamp;
  int x;
  int y;
  unsigned int width;
  unsigned int height;
  RRMode mode;
  int rotation;
  int noutput;
  RROutput *outputs;
  int rotations;
  int npossible;
  RROutput *possible;
} XRRCrtcInfo;

#define RRScreenChangeNotifyMask (1L << 0)
#define RRCrtcChangeNotifyMask (1L << 1)
#define RROutputChangeNotifyMask (1L << 2)
#define RROutputPropertyNotifyMask (1L << 3)

Status XRRQueryExtension(Display *display, int *event_base_return,
                         int *error_base_return);
Status XRRQueryVersion(Display *display, int *major_version_return,
                       int *minor_version_return);
XRRScreenResources *XRRGetScreenResources(Display *display, Window window);
void XRRFreeScreenResources(XRRScreenResources *resources);
XRROutputInfo *XRRGetOutputInfo(Display *display,
                                XRRScreenResources *resources,
                                RROutput output);
void XRRFreeOutputInfo(XRROutputInfo *output_info);
XRRCrtcInfo *XRRGetCrtcInfo(Display *display,
                            XRRScreenResources *resources,
                            RRCrtc crtc);
void XRRFreeCrtcInfo(XRRCrtcInfo *crtc_info);
void XRRSelectInput(Display *display, Window window,
                    int mask);
int XRRUpdateConfiguration(XEvent *event);

#endif
