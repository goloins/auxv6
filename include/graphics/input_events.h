#ifndef _GRAPHICS_INPUT_EVENTS_H_
#define _GRAPHICS_INPUT_EVENTS_H_

#include "types.h"

struct aux_mouse_event {
  short dx;
  short dy;
  uchar buttons;
  uchar changed;
};

struct aux_kbd_event {
  ushort keycode;
  ushort state;
  uchar value;
  uchar reserved[3];
};

#define AUX_KBD_VALUE_RELEASE 0
#define AUX_KBD_VALUE_PRESS   1
#define AUX_KBD_VALUE_REPEAT  2

#endif