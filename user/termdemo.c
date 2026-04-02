#include "types.h"
#include "auxv6/user.h"

static void
put(const char *s)
{
  write(1, s, strlen(s));
}

static void
put_utf8(uint cp)
{
  char b[4];

  if(cp <= 0x7F) {
    b[0] = (char)cp;
    write(1, b, 1);
  } else if(cp <= 0x7FF) {
    b[0] = (char)(0xC0 | ((cp >> 6) & 0x1F));
    b[1] = (char)(0x80 | (cp & 0x3F));
    write(1, b, 2);
  } else {
    b[0] = (char)(0xE0 | ((cp >> 12) & 0x0F));
    b[1] = (char)(0x80 | ((cp >> 6) & 0x3F));
    b[2] = (char)(0x80 | (cp & 0x3F));
    write(1, b, 3);
  }
}

int
main(void)
{
  int i;

  put("\033[2J\033[H");
  put("termdemo: ANSI/UTF-8/DEC graphics check\n\n");

  put("SGR colors: ");
  for(i = 31; i <= 37; i++) {
    char seq[16];
    seq[0] = '\033';
    seq[1] = '[';
    seq[2] = '0' + (i / 10);
    seq[3] = '0' + (i % 10);
    seq[4] = 'm';
    seq[5] = '0' + (i - 30);
    seq[6] = ' ';
    seq[7] = 0;
    put(seq);
  }
  put("\033[0m\n");

  put("UTF-8 box drawing: ");
  put_utf8(0x250C); put_utf8(0x2500); put_utf8(0x252C); put_utf8(0x2500); put_utf8(0x2510);
  put(" ");
  put_utf8(0x251C); put_utf8(0x2500); put_utf8(0x253C); put_utf8(0x2500); put_utf8(0x2524);
  put(" ");
  put_utf8(0x2514); put_utf8(0x2500); put_utf8(0x2534); put_utf8(0x2500); put_utf8(0x2518);
  put("\n");

  put("UTF-8 shading: ");
  put_utf8(0x2591); put_utf8(0x2592); put_utf8(0x2593); put_utf8(0x2588);
  put("\n");

  put("DEC graphics (ESC(0): ");
  put("\033(0lqqqk\033(B ");
  put("\033(0x\033(B");
  put("   ");
  put("\033(0x\033(B");
  put(" ");
  put("\033(0mqqqj\033(B");
  put("\n");

  put("Insert mode test: ");
  put("ABCDE");
  put("\033[5D\033[4hX\033[4l");
  put("  (expect XABCD)\n");

  put("No-wrap test (DECAWM off):\n");
  put("\033[?7l");
  for(i = 0; i < 90; i++)
    put("W");
  put("\033[?7h\n");

  put("Erase chars test: ");
  put("123456789");
  put("\033[5D\033[3X");
  put("  (middle should clear)\n");

  put("Alternate screen test starts in 1s...\n");
  sleep(100);
  put("\033[?1049h\033[2J\033[H");
  put("ALT SCREEN ACTIVE\n");
  put("If this works, leaving this mode restores the previous screen.\n");
  sleep(100);
  put("\033[?1049l");

  put("\nDone.\n");
  exit();
}
