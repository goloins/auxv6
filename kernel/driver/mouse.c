#include "types.h"
#include "x86.h"
#include "defs.h"
#include "traps.h"

#define KBSTATP             0x64
#define KBS_DIB             0x01
#define KBDATAP             0x60
#define KBS_IBF             0x02
#define MOUSE_CMD_ENABLE    0xA8
#define MOUSE_CMD_RDCFG     0x20
#define MOUSE_CMD_WRCFG     0x60
#define MOUSE_CMD_WRITEDEV  0xD4
#define MOUSE_DEV_DEFAULTS  0xF6
#define MOUSE_DEV_STREAM    0xF4
#define MOUSE_ACK           0xFA

static int mouse_ready;
static uchar mouse_packet[3];
static int mouse_packet_len;

static int
mouse_wait_input_clear(void)
{
  int i;

  for(i = 0; i < 100000; i++) {
    if((inb(KBSTATP) & KBS_IBF) == 0)
      return 0;
  }
  return -1;
}

static int
mouse_wait_output(uchar *out)
{
  int i;

  for(i = 0; i < 100000; i++) {
    if(inb(KBSTATP) & KBS_DIB) {
      if(out)
        *out = inb(KBDATAP);
      else
        inb(KBDATAP);
      return 0;
    }
  }
  return -1;
}

static int
mouse_write_device(uchar value)
{
  uchar ack;

  if(mouse_wait_input_clear() < 0)
    return -1;
  outb(KBSTATP, MOUSE_CMD_WRITEDEV);
  if(mouse_wait_input_clear() < 0)
    return -1;
  outb(KBDATAP, value);
  if(mouse_wait_output(&ack) < 0)
    return -1;
  return ack == MOUSE_ACK ? 0 : -1;
}

void
mouseinit(void)
{
  uchar cfg;

  if(mouse_wait_input_clear() < 0)
    return;
  outb(KBSTATP, MOUSE_CMD_ENABLE);

  if(mouse_wait_input_clear() < 0)
    return;
  outb(KBSTATP, MOUSE_CMD_RDCFG);
  if(mouse_wait_output(&cfg) < 0)
    return;

  cfg |= 0x02;
  cfg &= (uchar)~0x20;

  if(mouse_wait_input_clear() < 0)
    return;
  outb(KBSTATP, MOUSE_CMD_WRCFG);
  if(mouse_wait_input_clear() < 0)
    return;
  outb(KBDATAP, cfg);

  if(mouse_write_device(MOUSE_DEV_DEFAULTS) < 0)
    return;
  if(mouse_write_device(MOUSE_DEV_STREAM) < 0)
    return;

  mouse_packet_len = 0;
  mouse_ready = 1;
  ioapicenable(IRQ_MOUSE, 0);
}

void
mouseintr(void)
{
  uchar byte;
  int dx;
  int dy;
  uchar buttons;

  if(!mouse_ready)
    return;
  if((inb(KBSTATP) & KBS_DIB) == 0)
    return;

  byte = inb(KBDATAP);
  if(mouse_packet_len == 0 && (byte & 0x08) == 0)
    return;

  mouse_packet[mouse_packet_len++] = byte;
  if(mouse_packet_len < 3)
    return;
  mouse_packet_len = 0;

  if(mouse_packet[0] & 0xC0)
    return;

  dx = (signed char)mouse_packet[1];
  dy = (signed char)mouse_packet[2];
  buttons = (uchar)(mouse_packet[0] & 0x07);
  console_mouse_packet(dx, dy, buttons);
}