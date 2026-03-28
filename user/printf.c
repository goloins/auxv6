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
emit_padding(int fd, char *out, int *pos, int max, int n, char ch)
{
  while(n-- > 0)
    bufputc(fd, out, pos, max, ch);
}

static int
utoa_base(uint x, int base, char *buf)
{
  static char digits[] = "0123456789ABCDEF";
  int i;

  i = 0;
  do{
    buf[i++] = digits[x % base];
  }while((x /= base) != 0);

  return i;
}

static void
printstr_fmt(int fd, char *out, int *pos, int max,
             const char *s, int left, int width, int have_prec, int prec)
{
  int i;
  int n;

  if(s == 0)
    s = "(null)";

  n = 0;
  while(s[n] != 0 && (!have_prec || n < prec))
    n++;

  if(!left)
    emit_padding(fd, out, pos, max, width - n, ' ');

  for(i = 0; i < n; i++)
    bufputc(fd, out, pos, max, s[i]);

  if(left)
    emit_padding(fd, out, pos, max, width - n, ' ');
}

static void
printnum_fmt(int fd, char *out, int *pos, int max,
             uint x, int base, int neg, int left,
             int width, int zero, int have_prec, int prec,
             int alt_prefix)
{
  char buf[16];
  int i;
  int ndigits;
  int nzeros;
  int prefix_len;
  int total;
  int pad;
  char padch;

  i = utoa_base(x, base, buf);
  ndigits = i;

  if(have_prec && prec == 0 && x == 0)
    ndigits = 0;

  nzeros = 0;
  if(have_prec && prec > ndigits)
    nzeros = prec - ndigits;

  prefix_len = 0;
  if(neg)
    prefix_len = 1;
  if(alt_prefix)
    prefix_len += 2;

  total = prefix_len + nzeros + ndigits;
  pad = width - total;
  if(pad < 0)
    pad = 0;

  padch = ' ';
  if(zero && !left && !have_prec)
    padch = '0';

  if(!left && padch == ' ')
    emit_padding(fd, out, pos, max, pad, ' ');

  if(neg)
    bufputc(fd, out, pos, max, '-');
  if(alt_prefix){
    bufputc(fd, out, pos, max, '0');
    bufputc(fd, out, pos, max, 'x');
  }

  if(!left && padch == '0')
    emit_padding(fd, out, pos, max, pad, '0');

  emit_padding(fd, out, pos, max, nzeros, '0');
  while(--i >= 0){
    if(have_prec && prec == 0 && x == 0)
      break;
    bufputc(fd, out, pos, max, buf[i]);
  }

  if(left)
    emit_padding(fd, out, pos, max, pad, ' ');
}

static int
isdigitc(int c)
{
  return c >= '0' && c <= '9';
}

// Print to the given fd. Supports %d/%i, %u, %o, %x/%X/%p, %s, %c, %%
// and a useful subset of printf formatting: '-', '0', width, precision,
// and optional 'l' length modifier (no-op on 32-bit where long == int).
void
printf(int fd, const char *fmt, ...)
{
  char out[256];
  int c;
  int i;
  int pos;
  uint *ap;

  pos = 0;
  ap = (uint*)(void*)&fmt + 1;

  for(i = 0; fmt[i]; i++){
    if(fmt[i] != '%'){
      bufputc(fd, out, &pos, sizeof(out), fmt[i]);
      continue;
    }

    i++;
    if(fmt[i] == 0)
      break;

    // Flags
    int left = 0;
    int zero = 0;
    while(fmt[i] == '-' || fmt[i] == '0'){
      if(fmt[i] == '-')
        left = 1;
      else if(fmt[i] == '0')
        zero = 1;
      i++;
    }
    if(left)
      zero = 0;

    // Width
    int width = 0;
    while(isdigitc(fmt[i])){
      width = width * 10 + (fmt[i] - '0');
      i++;
    }

    // Precision
    int have_prec = 0;
    int prec = 0;
    if(fmt[i] == '.'){
      have_prec = 1;
      i++;
      while(isdigitc(fmt[i])){
        prec = prec * 10 + (fmt[i] - '0');
        i++;
      }
      zero = 0;
    }

    // Optional length modifier.
    if(fmt[i] == 'l')
      i++;

    c = fmt[i] & 0xff;
    if(c == 'd' || c == 'i'){
      int v;
      uint ux;
      int neg;

      v = (int)*ap;
      ap++;
      neg = v < 0;
      ux = neg ? (uint)(-v) : (uint)v;
      printnum_fmt(fd, out, &pos, sizeof(out), ux, 10, neg,
                   left, width, zero, have_prec, prec, 0);
    } else if(c == 'u'){
      uint v;

      v = *ap;
      ap++;
      printnum_fmt(fd, out, &pos, sizeof(out), v, 10, 0,
                   left, width, zero, have_prec, prec, 0);
    } else if(c == 'x' || c == 'X' || c == 'o'){
      uint v;
      int base;

      v = *ap;
      ap++;
      base = (c == 'o') ? 8 : 16;
      printnum_fmt(fd, out, &pos, sizeof(out), v, base, 0,
                   left, width, zero, have_prec, prec, 0);
    } else if(c == 'p'){
      uint v;

      v = *ap;
      ap++;
      printnum_fmt(fd, out, &pos, sizeof(out), v, 16, 0,
                   left, width, 1, 1, 8, 1);
    } else if(c == 's'){
      char *s;

      s = (char*)*ap;
      ap++;
      printstr_fmt(fd, out, &pos, sizeof(out), s, left, width, have_prec, prec);
    } else if(c == 'c'){
      char ch;

      ch = (char)*ap;
      ap++;
      if(!left)
        emit_padding(fd, out, &pos, sizeof(out), width - 1, ' ');
      bufputc(fd, out, &pos, sizeof(out), ch);
      if(left)
        emit_padding(fd, out, &pos, sizeof(out), width - 1, ' ');
    } else if(c == '%'){
      bufputc(fd, out, &pos, sizeof(out), '%');
    } else {
      // Unknown % sequence. Print it to draw attention.
      bufputc(fd, out, &pos, sizeof(out), '%');
      bufputc(fd, out, &pos, sizeof(out), c);
    }
  }

  bufflush(fd, out, &pos);
}
