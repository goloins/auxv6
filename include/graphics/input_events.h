#ifndef _GRAPHICS_INPUT_EVENTS_H_
#define _GRAPHICS_INPUT_EVENTS_H_

#include "types.h"

struct aux_mouse_event {
  short dx;
  short dy;
  uchar buttons;
  uchar changed;
};

#endif