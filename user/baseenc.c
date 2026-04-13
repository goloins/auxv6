#include "auxv6/user.h"
#include "stdio.h"
#include "string.h"

static const char b64_alphabet[] = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
static const char b32_alphabet[] = "ABCDEFGHIJKLMNOPQRSTUVWXYZ234567";

static int
prog_is_base32(const char *argv0)
{
  const char *p = argv0;
  while(*p)
    p++;
  while(p > argv0 && p[-1] != '/')
    p--;
  return strcmp(p, "base32") == 0;
}

static int
alpha_index(char c, int is32)
{
  const char *a;
  int i;

  a = is32 ? b32_alphabet : b64_alphabet;
  for(i = 0; a[i]; i++)
    if(a[i] == c)
      return i;
  if(!is32) {
    if(c == '-')
      return 62;
    if(c == '_')
      return 63;
  }
  if(c >= 'a' && c <= 'z') {
    for(i = 0; a[i]; i++)
      if(a[i] == c - 'a' + 'A')
        return i;
  }
  return -1;
}

static int
encode_stream(FILE *fp, int is32)
{
  int out_col;

  out_col = 0;
  if(is32) {
    uchar in[5];
    while(1) {
      int n = (int)fread(in, 1, 5, fp);
      uint32_t b0, b1, b2, b3, b4;
      char out[8];
      int i;
      int out_n;

      if(n <= 0)
        break;

      b0 = (n > 0) ? in[0] : 0;
      b1 = (n > 1) ? in[1] : 0;
      b2 = (n > 2) ? in[2] : 0;
      b3 = (n > 3) ? in[3] : 0;
      b4 = (n > 4) ? in[4] : 0;

      out[0] = b32_alphabet[(b0 >> 3) & 31];
      out[1] = b32_alphabet[((b0 << 2) | (b1 >> 6)) & 31];
      out[2] = (n > 1) ? b32_alphabet[(b1 >> 1) & 31] : '=';
      out[3] = (n > 1) ? b32_alphabet[((b1 << 4) | (b2 >> 4)) & 31] : '=';
      out[4] = (n > 2) ? b32_alphabet[((b2 << 1) | (b3 >> 7)) & 31] : '=';
      out[5] = (n > 3) ? b32_alphabet[(b3 >> 2) & 31] : '=';
      out[6] = (n > 3) ? b32_alphabet[((b3 << 3) | (b4 >> 5)) & 31] : '=';
      out[7] = (n > 4) ? b32_alphabet[b4 & 31] : '=';

      out_n = 8;
      for(i = 0; i < out_n; i++) {
        fputc(out[i], stdout);
        if(++out_col == 76) {
          fputc('\n', stdout);
          out_col = 0;
        }
      }

      if(n < 5)
        break;
    }
  } else {
    uchar in[3];
    while(1) {
      int n = (int)fread(in, 1, 3, fp);
      uint32_t v;
      char out[4];
      int i;

      if(n <= 0)
        break;

      v = ((uint32_t)in[0] << 16) |
          ((uint32_t)((n > 1) ? in[1] : 0) << 8) |
          (uint32_t)((n > 2) ? in[2] : 0);

      out[0] = b64_alphabet[(v >> 18) & 63];
      out[1] = b64_alphabet[(v >> 12) & 63];
      out[2] = (n > 1) ? b64_alphabet[(v >> 6) & 63] : '=';
      out[3] = (n > 2) ? b64_alphabet[v & 63] : '=';

      for(i = 0; i < 4; i++) {
        fputc(out[i], stdout);
        if(++out_col == 76) {
          fputc('\n', stdout);
          out_col = 0;
        }
      }

      if(n < 3)
        break;
    }
  }

  if(out_col)
    fputc('\n', stdout);
  return 0;
}

static int
decode_stream(FILE *fp, int is32)
{
  int vals[8];
  int vlen;
  int c;

  vlen = 0;
  while((c = fgetc(fp)) != EOF) {
    int v;

    if(c == '\n' || c == '\r' || c == '\t' || c == ' ')
      continue;

    if(c == '=')
      v = -2;
    else
      v = alpha_index((char)c, is32);

    if(v < -1)
      ;
    else if(v < 0)
      return -1;

    vals[vlen++] = v;

    if((!is32 && vlen == 4) || (is32 && vlen == 8)) {
      if(is32) {
        uint32_t a = (vals[0] < 0) ? 0U : (uint32_t)vals[0];
        uint32_t b = (vals[1] < 0) ? 0U : (uint32_t)vals[1];
        uint32_t d = (vals[2] < 0) ? 0U : (uint32_t)vals[2];
        uint32_t e = (vals[3] < 0) ? 0U : (uint32_t)vals[3];
        uint32_t f = (vals[4] < 0) ? 0U : (uint32_t)vals[4];
        uint32_t g = (vals[5] < 0) ? 0U : (uint32_t)vals[5];
        uint32_t h = (vals[6] < 0) ? 0U : (uint32_t)vals[6];
        uint32_t i = (vals[7] < 0) ? 0U : (uint32_t)vals[7];
        uchar o[5];
        int outn = 5;

        o[0] = (uchar)((a << 3) | (b >> 2));
        o[1] = (uchar)((b << 6) | (d << 1) | (e >> 4));
        o[2] = (uchar)((e << 4) | (f >> 1));
        o[3] = (uchar)((f << 7) | (g << 2) | (h >> 3));
        o[4] = (uchar)((h << 5) | i);

        if(vals[7] < 0) outn--;
        if(vals[6] < 0) outn--;
        if(vals[5] < 0) outn--;
        if(vals[4] < 0) outn--;
        if(vals[3] < 0) outn--;
        if(vals[2] < 0) outn--;

        if(write(1, o, outn) != outn)
          return -1;
      } else {
        uint32_t a = (vals[0] < 0) ? 0U : (uint32_t)vals[0];
        uint32_t b = (vals[1] < 0) ? 0U : (uint32_t)vals[1];
        uint32_t d = (vals[2] < 0) ? 0U : (uint32_t)vals[2];
        uint32_t e = (vals[3] < 0) ? 0U : (uint32_t)vals[3];
        uint32_t x = (a << 18) | (b << 12) | (d << 6) | e;
        uchar o[3];
        int outn = 3;

        o[0] = (uchar)((x >> 16) & 0xff);
        o[1] = (uchar)((x >> 8) & 0xff);
        o[2] = (uchar)(x & 0xff);

        if(vals[3] < 0) outn--;
        if(vals[2] < 0) outn--;

        if(write(1, o, outn) != outn)
          return -1;
      }
      vlen = 0;
    }
  }

  return vlen == 0 ? 0 : -1;
}

int
main(int argc, char *argv[])
{
  int is32;
  int decode;
  int i;
  int rc;

  is32 = prog_is_base32(argv[0]);
  decode = 0;
  rc = 0;

  i = 1;
  while(i < argc && argv[i][0] == '-') {
    if(strcmp(argv[i], "-d") == 0 || strcmp(argv[i], "--decode") == 0)
      decode = 1;
    else if(strcmp(argv[i], "--") == 0) {
      i++;
      break;
    } else {
      dprintf(2, "usage: %s [-d] [file ...]\n", is32 ? "base32" : "base64");
      return 1;
    }
    i++;
  }

  if(i == argc) {
    if((decode ? decode_stream(stdin, is32) : encode_stream(stdin, is32)) < 0)
      return 1;
    return 0;
  }

  for(; i < argc; i++) {
    FILE *fp;
    if(strcmp(argv[i], "-") == 0)
      fp = stdin;
    else
      fp = fopen(argv[i], "r");

    if(fp == 0) {
      dprintf(2, "%s: %s: cannot open\n", is32 ? "base32" : "base64", argv[i]);
      rc = 1;
      continue;
    }

    if((decode ? decode_stream(fp, is32) : encode_stream(fp, is32)) < 0) {
      dprintf(2, "%s: %s: invalid input\n", is32 ? "base32" : "base64", argv[i]);
      rc = 1;
    }

    if(fp != stdin)
      fclose(fp);
  }

  return rc;
}
