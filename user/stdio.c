#include "types.h"
#include "fcntl.h"
#include "errno.h"
#include "auxv6/user.h"
#include "stdio.h"

#define F_CAN_READ  0x01
#define F_CAN_WRITE 0x02

static FILE g_stdin = { .fd = 0, .flags = F_CAN_READ, .ungot = 0, .has_ungot = 0 };
static FILE g_stdout = { .fd = 1, .flags = F_CAN_WRITE, .ungot = 0, .has_ungot = 0 };
static FILE g_stderr = { .fd = 2, .flags = F_CAN_WRITE, .ungot = 0, .has_ungot = 0 };

FILE *stdin = &g_stdin;
FILE *stdout = &g_stdout;
FILE *stderr = &g_stderr;

static int
mode_flags(const char *mode, int *open_flags, int *caps)
{
  int plus;

  if(mode == 0 || mode[0] == 0)
    return -1;

  plus = (strchr(mode, '+') != 0);
  *caps = 0;

  switch(mode[0]){
  case 'r':
    *open_flags = plus ? O_RDWR : O_RDONLY;
    *caps = plus ? (F_CAN_READ | F_CAN_WRITE) : F_CAN_READ;
    return 0;
  case 'w':
    *open_flags = O_CREATE | O_TRUNC | (plus ? O_RDWR : O_WRONLY);
    *caps = plus ? (F_CAN_READ | F_CAN_WRITE) : F_CAN_WRITE;
    return 0;
  case 'a':
    *open_flags = O_CREATE | O_APPEND | (plus ? O_RDWR : O_WRONLY);
    *caps = plus ? (F_CAN_READ | F_CAN_WRITE) : F_CAN_WRITE;
    return 0;
  default:
    return -1;
  }
}

static char *
grow_line_buf(char *old, size_t copy_len, size_t new_cap)
{
  char *n;

  n = (char*)malloc((uint)new_cap);
  if(n == 0)
    return 0;

  if(old && copy_len)
    memmove(n, old, (int)copy_len);
  if(old)
    free(old);
  return n;
}

FILE *
fdopen(int fd, const char *mode)
{
  FILE *fp;
  int oflags;
  int caps;

  if(mode_flags(mode, &oflags, &caps) < 0)
    return 0;

  fp = (FILE*)malloc(sizeof(FILE));
  if(fp == 0)
    return 0;
  memset(fp, 0, sizeof(*fp));
  fp->fd = fd;
  fp->flags = caps;
  return fp;
}

FILE *
fopen(const char *path, const char *mode)
{
  int fd;
  int oflags;
  int caps;
  FILE *fp;

  if(mode_flags(mode, &oflags, &caps) < 0)
    return 0;

  fd = open(path, oflags);
  if(fd < 0)
    return 0;

  fp = (FILE*)malloc(sizeof(FILE));
  if(fp == 0){
    close(fd);
    return 0;
  }

  memset(fp, 0, sizeof(*fp));
  fp->fd = fd;
  fp->flags = caps;
  return fp;
}

FILE *
fmemopen(void *buf, size_t size, const char *mode)
{
  FILE *fp;
  int oflags;
  int caps;

  if(mode_flags(mode, &oflags, &caps) < 0)
    return 0;
  if(!(caps & F_CAN_READ))
    return 0;

  fp = (FILE*)malloc(sizeof(FILE));
  if(fp == 0)
    return 0;

  memset(fp, 0, sizeof(*fp));
  fp->fd = -1;
  fp->flags = caps;
  fp->is_mem = 1;
  fp->own_mem = 0;
  fp->mem = (const char*)buf;
  fp->mem_size = size;
  fp->mem_pos = 0;
  return fp;
}

int
fflush(FILE *fp)
{
  (void)fp;
  return 0;
}

int
fclose(FILE *fp)
{
  int rc;

  if(fp == 0)
    return -1;

  rc = 0;
  if(!fp->is_mem && fp->fd >= 0)
    rc = close(fp->fd);

  if(fp->own_mem && fp->mem)
    free((void*)fp->mem);

  if(fp != stdin && fp != stdout && fp != stderr)
    free(fp);
  else {
    fp->fd = -1;
    fp->eof = 1;
    fp->err = 0;
  }

  return rc;
}

int
ferror(FILE *fp)
{
  if(fp == 0)
    return 1;
  return fp->err;
}

int
feof(FILE *fp)
{
  if(fp == 0)
    return 1;
  return fp->eof;
}

void
clearerr(FILE *fp)
{
  if(fp == 0)
    return;
  fp->err = 0;
  fp->eof = 0;
}

int
fgetc(FILE *fp)
{
  uchar ch;
  int n;

  if(fp == 0 || !(fp->flags & F_CAN_READ))
    return EOF;

  if(fp->has_ungot){
    fp->has_ungot = 0;
    return fp->ungot & 0xff;
  }

  if(fp->is_mem){
    if(fp->mem_pos >= fp->mem_size){
      fp->eof = 1;
      return EOF;
    }
    ch = (uchar)fp->mem[fp->mem_pos++];
    return (int)ch;
  }

  n = read(fp->fd, &ch, 1);
  if(n == 1)
    return (int)ch;
  if(n == 0){
    fp->eof = 1;
    return EOF;
  }
  fp->err = 1;
  return EOF;
}

int
getc(FILE *fp)
{
  return fgetc(fp);
}

int
ungetc(int c, FILE *fp)
{
  if(fp == 0 || c == EOF)
    return EOF;
  fp->ungot = c & 0xff;
  fp->has_ungot = 1;
  fp->eof = 0;
  return fp->ungot;
}

int
fputc(int c, FILE *fp)
{
  char ch;
  int n;

  if(fp == 0 || !(fp->flags & F_CAN_WRITE))
    return EOF;

  ch = (char)(c & 0xff);

  if(fp->is_mem){
    fp->err = 1;
    return EOF;
  }

  n = write(fp->fd, &ch, 1);
  if(n == 1)
    return (uchar)ch;
  fp->err = 1;
  return EOF;
}

int
putc(int c, FILE *fp)
{
  return fputc(c, fp);
}

char *
fgets(char *s, int size, FILE *fp)
{
  int i;
  int ch;

  if(s == 0 || size <= 1 || fp == 0)
    return 0;

  for(i = 0; i < size - 1; i++){
    ch = fgetc(fp);
    if(ch == EOF)
      break;
    s[i] = (char)ch;
    if(ch == '\n'){
      i++;
      break;
    }
  }

  if(i == 0)
    return 0;

  s[i] = 0;
  return s;
}

int
fputs(const char *s, FILE *fp)
{
  int n;
  int off;
  int rc;

  if(s == 0 || fp == 0 || !(fp->flags & F_CAN_WRITE))
    return EOF;

  n = (int)strlen(s);
  off = 0;
  while(off < n){
    rc = write(fp->fd, s + off, n - off);
    if(rc <= 0){
      fp->err = 1;
      return EOF;
    }
    off += rc;
  }

  return 0;
}

int
puts(const char *s)
{
  if(fputs(s, stdout) == EOF)
    return EOF;
  if(fputc('\n', stdout) == EOF)
    return EOF;
  return 0;
}

size_t
fread(void *ptr, size_t size, size_t nmemb, FILE *fp)
{
  size_t want;
  int n;

  if(ptr == 0 || fp == 0 || size == 0 || nmemb == 0)
    return 0;

  want = size * nmemb;
  n = read(fp->fd, ptr, (int)want);
  if(n < 0){
    fp->err = 1;
    return 0;
  }
  if(n == 0)
    fp->eof = 1;

  return (size_t)n / size;
}

size_t
fwrite(const void *ptr, size_t size, size_t nmemb, FILE *fp)
{
  size_t want;
  int off;
  int n;

  if(ptr == 0 || fp == 0 || size == 0 || nmemb == 0)
    return 0;

  want = size * nmemb;
  off = 0;
  while((size_t)off < want){
    n = write(fp->fd, (const char*)ptr + off, (int)(want - off));
    if(n <= 0){
      fp->err = 1;
      break;
    }
    off += n;
  }

  return (size_t)off / size;
}

int
vfprintf(FILE *fp, const char *fmt, va_list ap)
{
  char small[256];
  int n;
  char *buf;
  va_list ap_copy;

  if(fp == 0)
    return -1;

  va_copy(ap_copy, ap);
  n = vsnprintf(small, sizeof(small), fmt, ap_copy);
  va_end(ap_copy);
  if(n < 0){
    fp->err = 1;
    return -1;
  }

  if(n < (int)sizeof(small)){
    if(fwrite(small, 1, (size_t)n, fp) != (size_t)n)
      return -1;
    return n;
  }

  buf = (char*)malloc((uint)n + 1);
  if(buf == 0){
    fp->err = 1;
    return -1;
  }

  va_copy(ap_copy, ap);
  vsnprintf(buf, (size_t)n + 1, fmt, ap_copy);
  va_end(ap_copy);
  if(fwrite(buf, 1, (size_t)n, fp) != (size_t)n){
    free(buf);
    return -1;
  }
  free(buf);
  return n;
}

int
fprintf(FILE *fp, const char *fmt, ...)
{
  va_list ap;
  int n;

  va_start(ap, fmt);
  n = vfprintf(fp, fmt, ap);
  va_end(ap);
  return n;
}

int
vprintf(const char *fmt, va_list ap)
{
  return vfprintf(stdout, fmt, ap);
}

int
printf(const char *fmt, ...)
{
  va_list ap;
  int n;

  va_start(ap, fmt);
  n = vfprintf(stdout, fmt, ap);
  va_end(ap);
  return n;
}

void
perror(const char *s)
{
  if(s && s[0])
    fprintf(stderr, "%s: errno=%d\n", s, errno);
  else
    fprintf(stderr, "errno=%d\n", errno);
}

ssize_t
getdelim(char **lineptr, size_t *n, int delim, FILE *fp)
{
  char *buf;
  size_t cap;
  size_t len;
  int ch;
  char *nbuf;

  if(lineptr == 0 || n == 0 || fp == 0)
    return -1;

  buf = *lineptr;
  cap = *n;
  len = 0;

  if(buf == 0 || cap == 0){
    cap = 128;
    buf = (char*)malloc((uint)cap);
    if(buf == 0)
      return -1;
  }

  while(1){
    ch = fgetc(fp);
    if(ch == EOF)
      break;

    if(len + 1 >= cap){
      cap *= 2;
      nbuf = grow_line_buf(buf, len, cap);
      if(nbuf == 0){
        return -1;
      }
      buf = nbuf;
      *n = cap;
    }

    buf[len++] = (char)ch;
    if(ch == delim)
      break;
  }

  if(len == 0 && ch == EOF){
    *lineptr = buf;
    *n = cap;
    return -1;
  }

  buf[len] = 0;
  *lineptr = buf;
  *n = cap;
  return (ssize_t)len;
}

ssize_t
getline(char **lineptr, size_t *n, FILE *fp)
{
  return getdelim(lineptr, n, '\n', fp);
}
