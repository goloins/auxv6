#include "../include/types.h"
#include "../include/stat.h"
#include "../include/user.h"

static void
bufflush(int fd, char *out, int *pos)
{
  if(*pos > 0){
    write(fd, out, *pos);
    *pos = 0;
  }
}

static void
bufputc(int fd, char *out, int *pos, int max, char c)
{
  if(*pos >= max)
    bufflush(fd, out, pos);
  out[(*pos)++] = c;
  if(c == '\n')
    bufflush(fd, out, pos);
}

static void
printint(int fd, char *out, int *pos, int max, int xx, int base, int sgn)
{
  static char digits[] = "0123456789ABCDEF";
  char buf[16];
  int i, neg;
  uint x;

  neg = 0;
  if(sgn && xx < 0){
    neg = 1;
    x = -xx;
  } else {
    x = xx;
  }

  i = 0;
  do{
    buf[i++] = digits[x % base];
  }while((x /= base) != 0);
  if(neg)
    buf[i++] = '-';

  while(--i >= 0)
    bufputc(fd, out, pos, max, buf[i]);
}

// Print to the given fd. Understands %d/%i, %u, %o, %x/%X/%p, %s, %c, %%
// and the %l length modifier (no-op on 32-bit where long == int).
void
printf(int fd, const char *fmt, ...)
{
  char out[256];
  char *s;
  int c, i, state;
  int pos;
  uint *ap;

  state = 0;
  pos = 0;
  ap = (uint*)(void*)&fmt + 1;
  for(i = 0; fmt[i]; i++){
    c = fmt[i] & 0xff;
    if(state == 0){
      if(c == '%'){
        state = '%';
      } else {
        bufputc(fd, out, &pos, sizeof(out), c);
      }
    } else if(state == '%'){
      if(c == 'l'){
        state = 'l';  // consume length modifier; long==int on 32-bit
        continue;
      }
      if(c == 'd' || c == 'i'){
        printint(fd, out, &pos, sizeof(out), *ap, 10, 1);
        ap++;
      } else if(c == 'u'){
        printint(fd, out, &pos, sizeof(out), *ap, 10, 0);
        ap++;
      } else if(c == 'x' || c == 'X' || c == 'p'){
        printint(fd, out, &pos, sizeof(out), *ap, 16, 0);
        ap++;
      } else if(c == 'o'){
        printint(fd, out, &pos, sizeof(out), *ap, 8, 0);
        ap++;
      } else if(c == 's'){
        s = (char*)*ap;
        ap++;
        if(s == 0)
          s = "(null)";
        while(*s != 0){
          bufputc(fd, out, &pos, sizeof(out), *s);
          s++;
        }
      } else if(c == 'c'){
        bufputc(fd, out, &pos, sizeof(out), *ap);
        ap++;
      } else if(c == '%'){
        bufputc(fd, out, &pos, sizeof(out), c);
      } else {
        // Unknown % sequence.  Print it to draw attention.
        bufputc(fd, out, &pos, sizeof(out), '%');
        bufputc(fd, out, &pos, sizeof(out), c);
      }
      state = 0;
    } else if(state == 'l'){
      // %l modifier: long == int on i686, delegate to same handlers.
      if(c == 'd' || c == 'i'){
        printint(fd, out, &pos, sizeof(out), *ap, 10, 1);
        ap++;
      } else if(c == 'u'){
        printint(fd, out, &pos, sizeof(out), *ap, 10, 0);
        ap++;
      } else if(c == 'x' || c == 'X'){
        printint(fd, out, &pos, sizeof(out), *ap, 16, 0);
        ap++;
      } else if(c == 'o'){
        printint(fd, out, &pos, sizeof(out), *ap, 8, 0);
        ap++;
      } else {
        bufputc(fd, out, &pos, sizeof(out), '%');
        bufputc(fd, out, &pos, sizeof(out), 'l');
        bufputc(fd, out, &pos, sizeof(out), c);
      }
      state = 0;
    }
  }

  bufflush(fd, out, &pos);
}
