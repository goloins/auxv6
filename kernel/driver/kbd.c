#include "types.h"
#include "x86.h"
#include "defs.h"
#include "kbd.h"
#include "spinlock.h"
#include "graphics/input_events.h"

uint kbd_shift_state;

struct kbd_event_state {
  struct spinlock lock;
  struct aux_kbd_event buf[256];
  uint r;
  uint w;
  int inited;
};

static struct kbd_event_state kbd_events;

static void
kbd_event_ensure_init(void)
{
  if(kbd_events.inited)
    return;
  initlock(&kbd_events.lock, "kbd_events");
  kbd_events.r = 0;
  kbd_events.w = 0;
  kbd_events.inited = 1;
}

static void
kbd_event_enqueue(ushort keycode, ushort state, uchar value)
{
  uint cap;
  struct aux_kbd_event evt;

  if(keycode == 0)
    return;

  kbd_event_ensure_init();

  evt.keycode = keycode;
  evt.state = state;
  evt.value = value;
  evt.reserved[0] = 0;
  evt.reserved[1] = 0;
  evt.reserved[2] = 0;

  acquire(&kbd_events.lock);
  cap = (uint)(sizeof(kbd_events.buf) / sizeof(kbd_events.buf[0]));
  if(kbd_events.w - kbd_events.r >= cap)
    kbd_events.r++;
  kbd_events.buf[kbd_events.w++ % cap] = evt;
  wakeup(&kbd_events.r);
  release(&kbd_events.lock);
}

static ushort
kbd_modstate_to_xmask(uint modstate)
{
  ushort xstate;

  xstate = 0;
  if(modstate & SHIFT)
    xstate |= (1U << 0);
  if(modstate & CTL)
    xstate |= (1U << 2);
  if(modstate & ALT)
    xstate |= (1U << 3);
  return xstate;
}

static ushort
kbd_scancode_to_keycode(uint data)
{
  return (ushort)normalmap[data];
}

int
kbdgetc(void)
{
  static uchar *charcode[4] = {
    normalmap, shiftmap, ctlmap, ctlmap
  };
  uint st, data, c;
  ushort keycode;
  ushort xstate;

  st = inb(KBSTATP);
  if((st & KBS_DIB) == 0)
    return -1;
  data = inb(KBDATAP);

  if(data == 0xE0){
    kbd_shift_state |= E0ESC;
    return 0;
  } else if(data & 0x80){
    // Key released
    data = (kbd_shift_state & E0ESC ? data : data & 0x7F);
    kbd_shift_state &= ~(shiftcode[data] | E0ESC);
    keycode = kbd_scancode_to_keycode(data);
    xstate = kbd_modstate_to_xmask(kbd_shift_state);
    kbd_event_enqueue(keycode, xstate, AUX_KBD_VALUE_RELEASE);
    return 0;
  } else if(kbd_shift_state & E0ESC){
    // Last character was an E0 escape; or with 0x80
    data |= 0x80;
    kbd_shift_state &= ~E0ESC;
  }

  kbd_shift_state |= shiftcode[data];
  kbd_shift_state ^= togglecode[data];

  keycode = kbd_scancode_to_keycode(data);
  xstate = kbd_modstate_to_xmask(kbd_shift_state);
  kbd_event_enqueue(keycode, xstate, AUX_KBD_VALUE_PRESS);

  if((kbd_shift_state & ALT) && data >= 0x3B && data <= 0x3E)
    return KEY_F1 + (data - 0x3B);

  c = charcode[kbd_shift_state & (CTL | SHIFT)][data];
  if(kbd_shift_state & CAPSLOCK){
    if('a' <= c && c <= 'z')
      c += 'A' - 'a';
    else if('A' <= c && c <= 'Z')
      c += 'a' - 'A';
  }
  return c;
}

void
kbdintr(void)
{
  consoleintr(kbdgetc);
}

uint
kbdmodstate(void)
{
  return kbd_shift_state;
}

int
kbd_event_read(struct aux_kbd_event *dst, int max_events, int blocking)
{
  int got;
  uint cap;

  if(dst == 0 || max_events <= 0)
    return -1;

  kbd_event_ensure_init();
  acquire(&kbd_events.lock);
  while(kbd_events.r == kbd_events.w) {
    if(!blocking) {
      release(&kbd_events.lock);
      return 0;
    }
    sleep(&kbd_events.r, &kbd_events.lock);
  }

  cap = (uint)(sizeof(kbd_events.buf) / sizeof(kbd_events.buf[0]));
  got = 0;
  while(got < max_events && kbd_events.r != kbd_events.w)
    dst[got++] = kbd_events.buf[kbd_events.r++ % cap];

  release(&kbd_events.lock);
  return got;
}

void
kbd_event_poll(int *rd, int *wr, int *err)
{
  kbd_event_ensure_init();
  acquire(&kbd_events.lock);
  if(rd)
    *rd = (kbd_events.r != kbd_events.w) ? 1 : 0;
  if(wr)
    *wr = 0;
  if(err)
    *err = 0;
  release(&kbd_events.lock);
}
