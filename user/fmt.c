/*
 * fmt.c - formatted string helpers split out of user/posix.c
 */

#include "stddef.h"
#include "stdarg.h"

static void
emit_str(char *buf, int *posp, int size, const char *s, int slen,
         int left, int width, int have_prec, int prec)
{
  int n;
  int pad;
  int i;

  if(s == 0)
    s = "(null)";
  n = 0;
  while(s[n])
    n++;
  if(have_prec && prec < n)
    n = prec;
  (void)slen;

  pad = width - n;
  if(pad < 0)
    pad = 0;

  if(!left)
    for(i = 0; i < pad; i++) { if(*posp < size - 1) buf[*posp] = ' '; (*posp)++; }
  for(i = 0; i < n; i++) { if(*posp < size - 1) buf[*posp] = s[i]; (*posp)++; }
  if(left)
    for(i = 0; i < pad; i++) { if(*posp < size - 1) buf[*posp] = ' '; (*posp)++; }
}

static void
u64_divmod_small(unsigned long long n, unsigned base,
                 unsigned long long *q_out, unsigned *r_out)
{
  unsigned long long q;
  unsigned long long r;
  int i;

  q = 0;
  r = 0;
  for(i = 63; i >= 0; i--){
    r = (r << 1) | ((n >> i) & 1ULL);
    if(r >= (unsigned long long)base){
      r -= (unsigned long long)base;
      q |= (1ULL << i);
    }
  }

  if(q_out)
    *q_out = q;
  if(r_out)
    *r_out = (unsigned)r;
}

static void
emit_uint(char *buf, int *posp, int size, unsigned long long v,
          int base, int upper, int neg, int left, int width,
          int zero_pad, int have_prec, int prec, int alt, int blank, int plus)
{
  char tmp[30];
  int dn;
  int prefix_len;
  char prefix[4];
  int total;
  int pad;
  int i;
  char padch;
  int nzeros;
  static const char *lo = "0123456789abcdef";
  static const char *hi = "0123456789ABCDEF";
  const char *digits;

  dn = 0;
  prefix_len = 0;
  digits = upper ? hi : lo;

  if(v == 0 && !(have_prec && prec == 0)) {
    tmp[dn++] = '0';
  } else {
    unsigned long long t;

    t = v;
    while(t > 0) {
      unsigned rem;
      unsigned long long q;

      u64_divmod_small(t, (unsigned)base, &q, &rem);
      tmp[dn++] = digits[rem];
      t = q;
    }
  }

  if(neg)
    prefix[prefix_len++] = '-';
  else if(plus)
    prefix[prefix_len++] = '+';
  else if(blank)
    prefix[prefix_len++] = ' ';
  if(alt && base == 16 && v) {
    prefix[prefix_len++] = '0';
    prefix[prefix_len++] = upper ? 'X' : 'x';
  }
  if(alt && base == 8 && (dn == 0 || tmp[dn - 1] != '0'))
    prefix[prefix_len++] = '0';

  nzeros = 0;
  if(have_prec && prec > dn)
    nzeros = prec - dn;
  total = prefix_len + nzeros + dn;
  pad = width - total;
  if(pad < 0)
    pad = 0;

  padch = (zero_pad && !left && !have_prec) ? '0' : ' ';

  if(!left && padch == ' ')
    for(i = 0; i < pad; i++) { if(*posp < size - 1) buf[*posp] = ' '; (*posp)++; }
  for(i = 0; i < prefix_len; i++) { if(*posp < size - 1) buf[*posp] = prefix[i]; (*posp)++; }
  if(!left && padch == '0')
    for(i = 0; i < pad; i++) { if(*posp < size - 1) buf[*posp] = '0'; (*posp)++; }
  for(i = 0; i < nzeros; i++) { if(*posp < size - 1) buf[*posp] = '0'; (*posp)++; }
  for(i = dn - 1; i >= 0; i--) { if(*posp < size - 1) buf[*posp] = tmp[i]; (*posp)++; }
  if(left)
    for(i = 0; i < pad; i++) { if(*posp < size - 1) buf[*posp] = ' '; (*posp)++; }
}

int
vsnprintf(char *buf, size_t size, const char *fmt, va_list ap)
{
  int pos;
  int c;

  pos = 0;
  if(size == 0)
    buf = 0;

  while((c = *fmt++) != 0) {
    if(c != '%') {
      if(buf && pos < (int)size - 1)
        buf[pos] = (char)c;
      pos++;
      continue;
    }

    {
      int left;
      int plus;
      int blank;
      int alt;
      int zero_pad;
      int width;
      int have_prec;
      int prec;
      int is_ll;
      int is_l;
      int is_hh;
      int is_h;
      int is_z;

      left = 0;
      plus = 0;
      blank = 0;
      alt = 0;
      zero_pad = 0;
      for(;;) {
        c = *fmt++;
        if(c == '-')
          left = 1;
        else if(c == '+')
          plus = 1;
        else if(c == ' ')
          blank = 1;
        else if(c == '#')
          alt = 1;
        else if(c == '0')
          zero_pad = 1;
        else
          break;
      }
      if(left)
        zero_pad = 0;

      width = 0;
      if(c == '*') {
        width = va_arg(ap, int);
        if(width < 0) {
          left = 1;
          width = -width;
        }
        c = *fmt++;
      } else {
        while(c >= '0' && c <= '9') {
          width = width * 10 + (c - '0');
          c = *fmt++;
        }
      }

      have_prec = 0;
      prec = 0;
      if(c == '.') {
        have_prec = 1;
        c = *fmt++;
        if(c == '*') {
          prec = va_arg(ap, int);
          if(prec < 0) {
            have_prec = 0;
            prec = 0;
          }
          c = *fmt++;
        } else {
          while(c >= '0' && c <= '9') {
            prec = prec * 10 + (c - '0');
            c = *fmt++;
          }
        }
      }

      is_ll = 0;
      is_l = 0;
      is_hh = 0;
      is_h = 0;
      is_z = 0;
      if(c == 'l') {
        c = *fmt++;
        if(c == 'l') {
          is_ll = 1;
          c = *fmt++;
        } else {
          is_l = 1;
        }
      } else if(c == 'h') {
        c = *fmt++;
        if(c == 'h') {
          is_hh = 1;
          c = *fmt++;
        } else {
          is_h = 1;
        }
      } else if(c == 'z') {
        is_z = 1;
        c = *fmt++;
      } else if(c == 't' || c == 'j') {
        c = *fmt++;
      }

      if(c == 'd' || c == 'i') {
        long long v;
        unsigned long long uv;

        if(is_ll)
          v = va_arg(ap, long long);
        else if(is_l)
          v = va_arg(ap, long);
        else if(is_hh)
          v = (signed char)va_arg(ap, int);
        else if(is_h)
          v = (short)va_arg(ap, int);
        else if(is_z)
          v = (long)va_arg(ap, size_t);
        else
          v = va_arg(ap, int);
        uv = (v < 0) ? (unsigned long long)(-v) : (unsigned long long)v;
        emit_uint(buf, &pos, (int)size, uv, 10, 0, v < 0,
                  left, width, zero_pad, have_prec, prec, 0, blank, plus);
      } else if(c == 'u' || c == 'o' || c == 'x' || c == 'X') {
        unsigned long long v;
        int base;
        int upper;

        base = (c == 'o') ? 8 : (c == 'u') ? 10 : 16;
        upper = (c == 'X');
        if(is_ll)
          v = va_arg(ap, unsigned long long);
        else if(is_l)
          v = va_arg(ap, unsigned long);
        else if(is_hh)
          v = (unsigned char)va_arg(ap, unsigned);
        else if(is_h)
          v = (unsigned short)va_arg(ap, unsigned);
        else if(is_z)
          v = va_arg(ap, size_t);
        else
          v = va_arg(ap, unsigned);
        emit_uint(buf, &pos, (int)size, v, base, upper, 0,
                  left, width, zero_pad, have_prec, prec, alt, 0, 0);
      } else if(c == 'p') {
        unsigned long v;

        v = (unsigned long)va_arg(ap, void*);
        emit_uint(buf, &pos, (int)size, (unsigned long long)v, 16, 0, 0,
                  left, width, 1, 1, 8, 1, 0, 0);
      } else if(c == 's') {
        const char *s;

        s = va_arg(ap, const char*);
        emit_str(buf, &pos, (int)size, s, 0, left, width, have_prec, prec);
      } else if(c == 'c') {
        char ch;
        int pad;
        int i;

        ch = (char)va_arg(ap, int);
        pad = width - 1;
        if(pad < 0)
          pad = 0;
        if(!left)
          for(i = 0; i < pad; i++) { if(pos < (int)size - 1) buf[pos] = ' '; pos++; }
        if(pos < (int)size - 1)
          buf[pos] = (char)ch;
        pos++;
        if(left)
          for(i = 0; i < pad; i++) { if(pos < (int)size - 1) buf[pos] = ' '; pos++; }
      } else if(c == '%') {
        if(pos < (int)size - 1)
          buf[pos] = '%';
        pos++;
      } else if(c == 'n') {
        (void)va_arg(ap, int*);
      } else {
        if(pos < (int)size - 1) {
          buf[pos] = '%';
          pos++;
        }
        if(pos < (int)size - 1) {
          buf[pos] = (char)c;
          pos++;
        }
      }
    }
  }

  if(buf) {
    if(pos < (int)size)
      buf[pos] = '\0';
    else if(size > 0)
      buf[size - 1] = '\0';
  }
  return pos;
}

int
snprintf(char *buf, size_t size, const char *fmt, ...)
{
  va_list ap;
  int n;

  va_start(ap, fmt);
  n = vsnprintf(buf, size, fmt, ap);
  va_end(ap);
  return n;
}

int
vsprintf(char *buf, const char *fmt, va_list ap)
{
  return vsnprintf(buf, (size_t)0x7fffffff, fmt, ap);
}

int
sprintf(char *buf, const char *fmt, ...)
{
  va_list ap;
  int n;

  va_start(ap, fmt);
  n = vsprintf(buf, fmt, ap);
  va_end(ap);
  return n;
}

int
sscanf(const char *str, const char *fmt, ...)
{
  (void)str;
  (void)fmt;
  return 0;
}