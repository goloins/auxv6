// Intel 8250 serial port (UART).

#include "types.h"
#include "defs.h"
#include "param.h"
#include "traps.h"
#include "spinlock.h"
#include "sleeplock.h"
#include "fs.h"
#include "file.h"
#include "mmu.h"
#include "proc.h"
#include "termios.h"
#include "sys/ioctl.h"
#include "x86.h"

#define COM1    0x3f8

#define UART_BAUD_MASK 0010017U

#define UART_MCR_DTR 0x01
#define UART_MCR_RTS 0x02
#define UART_MCR_OUT2 0x08

#define UART_MSR_CTS 0x10
#define UART_MSR_DSR 0x20
#define UART_MSR_RI  0x40
#define UART_MSR_DCD 0x80

static int uart;    // is there a uart?
static int uartgetc(void);
static uchar uart_mcr_shadow;

static void
uart_write_mcr(uchar mcr)
{
  uart_mcr_shadow = mcr;
  outb(COM1+4, uart_mcr_shadow);
}

static ushort
uart_divisor_from_cflag(uint c_cflag)
{
  switch(c_cflag & UART_BAUD_MASK) {
  case B0:      return 0;
  case B50:     return 2304;
  case B75:     return 1536;
  case B110:    return 1047;
  case B134:    return 859;
  case B150:    return 768;
  case B200:    return 576;
  case B300:    return 384;
  case B600:    return 192;
  case B1200:   return 96;
  case B1800:   return 64;
  case B2400:   return 48;
  case B4800:   return 24;
  case B9600:   return 12;
  case B19200:  return 6;
  case B38400:  return 3;
  case B57600:  return 2;
  case B115200: return 1;
  case B230400: return 1; /* 8250 cannot generate 230400 exactly without custom clocking */
  default:      return 12;
  }
}

void
uart_apply_termios(uint c_cflag)
{
  ushort div;
  uchar lcr;
  uchar mcr;

  if(!uart)
    return;

  div = uart_divisor_from_cflag(c_cflag);

  lcr = 0;
  switch(c_cflag & CSIZE) {
  case CS5:
    lcr |= 0x00;
    break;
  case CS6:
    lcr |= 0x01;
    break;
  case CS7:
    lcr |= 0x02;
    break;
  case CS8:
  default:
    lcr |= 0x03;
    break;
  }

  if(c_cflag & CSTOPB)
    lcr |= 0x04;
  if(c_cflag & PARENB) {
    lcr |= 0x08;
    if(!(c_cflag & PARODD))
      lcr |= 0x10;
  }

  mcr = uart_mcr_shadow & UART_MCR_OUT2;
  if(!(c_cflag & HUPCL) || div != 0)
    mcr |= (UART_MCR_DTR | UART_MCR_RTS);

  if(div == 0)
    div = 12;

  outb(COM1+3, 0x80);
  outb(COM1+0, div & 0xFF);
  outb(COM1+1, (div >> 8) & 0xFF);
  outb(COM1+3, lcr);
  uart_write_mcr(mcr);
  outb(COM1+1, 0x01);
}

uint
uart_get_modem_bits(void)
{
  uint bits;
  uchar msr;

  if(!uart)
    return 0;

  bits = 0;
  if(uart_mcr_shadow & UART_MCR_DTR)
    bits |= TIOCM_DTR;
  if(uart_mcr_shadow & UART_MCR_RTS)
    bits |= TIOCM_RTS;

  msr = inb(COM1+6);
  if(msr & UART_MSR_CTS)
    bits |= TIOCM_CTS;
  if(msr & UART_MSR_DSR)
    bits |= TIOCM_DSR;
  if(msr & UART_MSR_RI)
    bits |= TIOCM_RI;
  if(msr & UART_MSR_DCD)
    bits |= TIOCM_CD;

  return bits;
}

void
uart_set_modem_control(uint set_mask, uint clear_mask)
{
  uchar mcr;

  if(!uart)
    return;

  mcr = uart_mcr_shadow;

  if(set_mask & TIOCM_DTR)
    mcr |= UART_MCR_DTR;
  if(set_mask & TIOCM_RTS)
    mcr |= UART_MCR_RTS;

  if(clear_mask & TIOCM_DTR)
    mcr &= ~UART_MCR_DTR;
  if(clear_mask & TIOCM_RTS)
    mcr &= ~UART_MCR_RTS;

  if(mcr & (UART_MCR_DTR | UART_MCR_RTS))
    mcr |= UART_MCR_OUT2;

  uart_write_mcr(mcr);
}

static int
uartgetc_tap(void)
{
  int c;

  c = uartgetc();
  if(c >= 0)
    serial_rx_char(c);
  return c;
}

void
uartinit(void)
{
  char *p;

  // Turn off the FIFO
  outb(COM1+2, 0);

  // Bring up UART with default serial termios policy.
  uart_mcr_shadow = 0;
  uart_write_mcr(0);
  outb(COM1+1, 0x00);

  // If status is 0xFF, no serial port.
  if(inb(COM1+5) == 0xFF)
    return;
  uart = 1;

  uart_apply_termios(CREAD | CS8 | CLOCAL | B9600);
  serial_modem_update(uart_get_modem_bits());

  // Acknowledge pre-existing interrupt conditions;
  // enable interrupts.
  inb(COM1+2);
  inb(COM1+0);
  ioapicenable(IRQ_COM1, 0);

  // Announce that we're here.
  for(p="a/uxv6...\n"; *p; p++)
    uartputc(*p);
}

void
uartputc(int c)
{
  int i;

  if(!uart)
    return;
  for(i = 0; i < 128 && !(inb(COM1+5) & 0x20); i++)
    microdelay(10);
  outb(COM1+0, c);
}

static int
uartgetc(void)
{
  if(!uart)
    return -1;
  if(!(inb(COM1+5) & 0x01))
    return -1;
  return inb(COM1+0);
}

void
uartintr(void)
{
  serial_modem_update(uart_get_modem_bits());
  consoleintr(uartgetc_tap);
  serial_modem_update(uart_get_modem_bits());
}
