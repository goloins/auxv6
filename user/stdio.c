#include "types.h"
#include "ctype.h"
#include "fcntl.h"
#include "errno.h"
#include "inttypes.h"
#include "stdlib.h"
#include "string.h"
#include "auxv6/user.h"
#include "stdio.h"

#define F_CAN_READ  0x01
#define F_CAN_WRITE 0x02

enum scan_length {
  SCAN_LEN_DEFAULT,
  SCAN_LEN_CHAR,
  SCAN_LEN_SHORT,
  SCAN_LEN_LONG,
  SCAN_LEN_LLONG,
  SCAN_LEN_SIZE,
  SCAN_LEN_PTRDIFF,
  SCAN_LEN_INTMAX,
};

static FILE g_stdin = {
  .fd = 0,
  .flags = F_CAN_READ,
  .ungot = 0,
  .has_ungot = 0,
  .buf_mode = _IONBF,
};
static FILE g_stdout = {
  .fd = 1,
  .flags = F_CAN_WRITE,
  .ungot = 0,
  .has_ungot = 0,
  .buf_mode = _IONBF,
};
static FILE g_stderr = {
  .fd = 2,
  .flags = F_CAN_WRITE,
  .ungot = 0,
  .has_ungot = 0,
  .buf_mode = _IONBF,
};

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

static int
file_logical_pos(FILE *fp, off_t *pos)
{
  off_t cur;

  if(fp == 0 || pos == 0) {
    errno = EINVAL;
    return -1;
  }

  if(fp->is_mem) {
    cur = (off_t)fp->mem_pos;
    if(fp->has_ungot && cur > 0)
      cur--;
    *pos = cur;
    return 0;
  }

  cur = lseek(fp->fd, 0, SEEK_CUR);
  if(cur < 0) {
    fp->err = 1;
    return -1;
  }

  if(fp->buf_len > 0)
    cur += (off_t)fp->buf_len;
  if(fp->has_ungot && cur > 0)
    cur--;
  *pos = cur;
  return 0;
}

static void
file_release_buffer(FILE *fp)
{
  if(fp == 0)
    return;
  if(fp->buf_owned && fp->buf)
    free(fp->buf);
  fp->buf = 0;
  fp->buf_size = 0;
  fp->buf_len = 0;
  fp->buf_owned = 0;
}

static int
file_flush_output(FILE *fp)
{
  size_t off;
  int n;

  if(fp == 0)
    return 0;
  if(fp->buf_len == 0)
    return 0;
  if(fp->is_mem || fp->fd < 0) {
    fp->err = 1;
    errno = EBADF;
    return EOF;
  }

  off = 0;
  while(off < fp->buf_len) {
    n = write(fp->fd, fp->buf + off, (int)(fp->buf_len - off));
    if(n <= 0) {
      fp->err = 1;
      return EOF;
    }
    off += (size_t)n;
  }

  fp->buf_len = 0;
  return 0;
}

static int
file_can_buffer_output(FILE *fp)
{
  return fp != 0 && !fp->is_mem && (fp->flags & F_CAN_WRITE) != 0 &&
         fp->buf_mode != _IONBF && fp->buf != 0 && fp->buf_size > 0;
}

static int
file_write_byte(FILE *fp, char ch)
{
  int n;

  if(fp == 0 || !(fp->flags & F_CAN_WRITE))
    return EOF;

  if(file_can_buffer_output(fp)) {
    if(fp->buf_len == fp->buf_size && file_flush_output(fp) == EOF)
      return EOF;
    fp->buf[fp->buf_len++] = ch;
    if(fp->buf_mode == _IOLBF && ch == '\n') {
      if(file_flush_output(fp) == EOF)
        return EOF;
    }
    return (uchar)ch;
  }

  if(fp->is_mem) {
    fp->err = 1;
    return EOF;
  }

  n = write(fp->fd, &ch, 1);
  if(n == 1)
    return (uchar)ch;
  fp->err = 1;
  return EOF;
}

static void
file_clear_pushback(FILE *fp)
{
  if(fp == 0)
    return;
  fp->has_ungot = 0;
  fp->eof = 0;
}

static int
scan_parse_integer(const char *src, int width, int base, int is_signed,
                   int *consumed, long long *svalue,
                   unsigned long long *uvalue)
{
  char stackbuf[64];
  char *buf;
  char *end;
  size_t len;
  unsigned long long tmp_unsigned;
  long long tmp_signed;

  if(src == 0 || consumed == 0)
    return -1;

  len = strlen(src);
  if(width >= 0 && (size_t)width < len)
    len = (size_t)width;
  if(len == 0) {
    *consumed = 0;
    return 0;
  }

  if(len < sizeof(stackbuf))
    buf = stackbuf;
  else {
    buf = (char*)malloc((uint)len + 1);
    if(buf == 0)
      return -1;
  }

  memmove(buf, src, len);
  buf[len] = '\0';

  if(is_signed) {
    tmp_signed = strtoll(buf, &end, base);
    if(end != buf && svalue)
      *svalue = tmp_signed;
  } else {
    tmp_unsigned = strtoull(buf, &end, base);
    if(end != buf && uvalue)
      *uvalue = tmp_unsigned;
  }

  *consumed = (int)(end - buf);
  if(buf != stackbuf)
    free(buf);
  return *consumed > 0;
}

static void
scan_store_signed(va_list *ap, int len, long long value)
{
  switch(len) {
  case SCAN_LEN_CHAR:
    *va_arg(*ap, signed char*) = (signed char)value;
    break;
  case SCAN_LEN_SHORT:
    *va_arg(*ap, short*) = (short)value;
    break;
  case SCAN_LEN_LONG:
    *va_arg(*ap, long*) = (long)value;
    break;
  case SCAN_LEN_LLONG:
    *va_arg(*ap, long long*) = (long long)value;
    break;
  case SCAN_LEN_SIZE:
    *va_arg(*ap, ssize_t*) = (ssize_t)value;
    break;
  case SCAN_LEN_PTRDIFF:
    *va_arg(*ap, ptrdiff_t*) = (ptrdiff_t)value;
    break;
  case SCAN_LEN_INTMAX:
    *va_arg(*ap, intmax_t*) = (intmax_t)value;
    break;
  default:
    *va_arg(*ap, int*) = (int)value;
    break;
  }
}

static void
scan_store_unsigned(va_list *ap, int len, unsigned long long value)
{
  switch(len) {
  case SCAN_LEN_CHAR:
    *va_arg(*ap, unsigned char*) = (unsigned char)value;
    break;
  case SCAN_LEN_SHORT:
    *va_arg(*ap, unsigned short*) = (unsigned short)value;
    break;
  case SCAN_LEN_LONG:
    *va_arg(*ap, unsigned long*) = (unsigned long)value;
    break;
  case SCAN_LEN_LLONG:
    *va_arg(*ap, unsigned long long*) = (unsigned long long)value;
    break;
  case SCAN_LEN_SIZE:
    *va_arg(*ap, size_t*) = (size_t)value;
    break;
  case SCAN_LEN_PTRDIFF:
    *va_arg(*ap, uintptr_t*) = (uintptr_t)value;
    break;
  case SCAN_LEN_INTMAX:
    *va_arg(*ap, uintmax_t*) = (uintmax_t)value;
    break;
  default:
    *va_arg(*ap, unsigned int*) = (unsigned int)value;
    break;
  }
}

static void
scan_store_count(va_list *ap, int len, size_t count)
{
  switch(len) {
  case SCAN_LEN_CHAR:
    *va_arg(*ap, signed char*) = (signed char)count;
    break;
  case SCAN_LEN_SHORT:
    *va_arg(*ap, short*) = (short)count;
    break;
  case SCAN_LEN_LONG:
    *va_arg(*ap, long*) = (long)count;
    break;
  case SCAN_LEN_LLONG:
    *va_arg(*ap, long long*) = (long long)count;
    break;
  case SCAN_LEN_SIZE:
    *va_arg(*ap, ssize_t*) = (ssize_t)count;
    break;
  case SCAN_LEN_PTRDIFF:
    *va_arg(*ap, ptrdiff_t*) = (ptrdiff_t)count;
    break;
  case SCAN_LEN_INTMAX:
    *va_arg(*ap, intmax_t*) = (intmax_t)count;
    break;
  default:
    *va_arg(*ap, int*) = (int)count;
    break;
  }
}

static int
scan_scanset_match(const char *set, const char *set_end, int c)
{
  const char *p;
  int invert;
  int match;
  int first;

  p = set;
  invert = 0;
  if(p < set_end && *p == '^') {
    invert = 1;
    p++;
  }

  match = 0;
  first = 1;
  while(p < set_end) {
    int start;
    int end;

    start = (uchar)*p++;
    if(first && start == ']') {
      if(c == ']')
        match = 1;
      first = 0;
      continue;
    }

    if(start == '-' && (first || p == set_end)) {
      if(c == '-')
        match = 1;
      first = 0;
      continue;
    }

    end = start;
    if(p + 1 < set_end && *p == '-' && p[1] != ']') {
      p++;
      end = (uchar)*p++;
    }

    if(c >= start && c <= end)
      match = 1;
    first = 0;
  }

  return invert ? !match : match;
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
  fp->buf_mode = _IONBF;
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
  fp->buf_mode = _IONBF;
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
  fp->buf_mode = _IONBF;
  return fp;
}

int
fflush(FILE *fp)
{
  if(fp == 0) {
    if(fflush(stdout) == EOF)
      return EOF;
    if(fflush(stderr) == EOF)
      return EOF;
    return 0;
  }

  if(file_flush_output(fp) == EOF)
    return EOF;
  return 0;
}

int
setvbuf(FILE *fp, char *buf, int mode, size_t size)
{
  char *newbuf;
  int own;

  if(fp == 0) {
    errno = EINVAL;
    return -1;
  }
  if(mode != _IOFBF && mode != _IOLBF && mode != _IONBF) {
    errno = EINVAL;
    return -1;
  }
  if(fflush(fp) == EOF)
    return -1;

  if(mode != _IONBF && (fp->is_mem || !(fp->flags & F_CAN_WRITE))) {
    errno = ENOSYS;
    return -1;
  }

  file_release_buffer(fp);
  fp->buf_mode = _IONBF;

  if(mode == _IONBF)
    return 0;

  if(size == 0)
    size = BUFSIZ;

  own = 0;
  newbuf = buf;
  if(newbuf == 0) {
    newbuf = (char*)malloc((uint)size);
    if(newbuf == 0) {
      errno = ENOMEM;
      return -1;
    }
    own = 1;
  }

  fp->buf = newbuf;
  fp->buf_size = size;
  fp->buf_len = 0;
  fp->buf_mode = mode;
  fp->buf_owned = own;
  return 0;
}

void
setbuf(FILE *fp, char *buf)
{
  if(buf == 0)
    (void)setvbuf(fp, 0, _IONBF, 0);
  else
    (void)setvbuf(fp, buf, _IOFBF, BUFSIZ);
}

void
setlinebuf(FILE *fp)
{
  (void)setvbuf(fp, 0, _IOLBF, 0);
}

int
fseeko(FILE *fp, off_t offset, int whence)
{
  off_t base;
  off_t target;
  off_t rc;

  if(fp == 0) {
    errno = EINVAL;
    return -1;
  }

  if(!fp->is_mem && fflush(fp) == EOF)
    return -1;

  if(fp->is_mem) {
    switch(whence) {
    case SEEK_SET:
      base = 0;
      break;
    case SEEK_CUR:
      if(file_logical_pos(fp, &base) < 0)
        return -1;
      break;
    case SEEK_END:
      base = (off_t)fp->mem_size;
      break;
    default:
      errno = EINVAL;
      fp->err = 1;
      return -1;
    }

    target = base + offset;
    if(target < 0 || (size_t)target > fp->mem_size) {
      errno = EINVAL;
      fp->err = 1;
      return -1;
    }

    fp->mem_pos = (size_t)target;
    file_clear_pushback(fp);
    return 0;
  }

  if(fp->has_ungot && whence == SEEK_CUR)
    offset--;

  rc = lseek(fp->fd, offset, whence);
  if(rc < 0) {
    fp->err = 1;
    return -1;
  }

  file_clear_pushback(fp);
  return 0;
}

int
fseek(FILE *fp, long offset, int whence)
{
  return fseeko(fp, (off_t)offset, whence);
}

off_t
ftello(FILE *fp)
{
  off_t pos;

  if(file_logical_pos(fp, &pos) < 0)
    return (off_t)-1;
  return pos;
}

long
ftell(FILE *fp)
{
  return (long)ftello(fp);
}

void
rewind(FILE *fp)
{
  if(fp == 0)
    return;
  fseeko(fp, 0, SEEK_SET);
  clearerr(fp);
}

int
fgetpos(FILE *fp, fpos_t *pos)
{
  off_t cur;

  if(pos == 0) {
    errno = EINVAL;
    return -1;
  }

  cur = ftello(fp);
  if(cur < 0)
    return -1;
  *pos = (fpos_t)cur;
  return 0;
}

int
fsetpos(FILE *fp, const fpos_t *pos)
{
  if(pos == 0) {
    errno = EINVAL;
    return -1;
  }
  return fseeko(fp, (off_t)*pos, SEEK_SET);
}

int
fclose(FILE *fp)
{
  int rc;
  int flush_rc;

  if(fp == 0)
    return -1;

  flush_rc = fflush(fp);
  rc = 0;
  if(!fp->is_mem && fp->fd >= 0)
    rc = close(fp->fd);

  file_release_buffer(fp);
  if(fp->own_mem && fp->mem)
    free((void*)fp->mem);

  if(fp != stdin && fp != stdout && fp != stderr)
    free(fp);
  else {
    fp->fd = -1;
    fp->eof = 1;
    fp->err = 0;
    fp->buf_mode = _IONBF;
  }

  if(flush_rc == EOF)
    return EOF;
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

  if(fp == 0 || !(fp->flags & F_CAN_WRITE))
    return EOF;

  ch = (char)(c & 0xff);
  return file_write_byte(fp, ch);
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
  while(off < n) {
    rc = fputc((uchar)s[off], fp);
    if(rc == EOF)
      return EOF;
    off++;
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
  char *dst;
  size_t done;
  size_t want;
  size_t avail;
  size_t chunk;
  int n;

  if(ptr == 0 || fp == 0 || size == 0 || nmemb == 0)
    return 0;
  if(!(fp->flags & F_CAN_READ))
    return 0;

  dst = (char*)ptr;
  want = size * nmemb;
  done = 0;

  if(fp->has_ungot && want > 0) {
    dst[done++] = (char)(fp->ungot & 0xff);
    fp->has_ungot = 0;
  }

  if(done == want)
    return nmemb;

  if(fp->is_mem) {
    if(fp->mem_pos >= fp->mem_size) {
      fp->eof = 1;
      return done / size;
    }

    avail = fp->mem_size - fp->mem_pos;
    chunk = want - done;
    if(chunk > avail)
      chunk = avail;

    memmove(dst + done, fp->mem + fp->mem_pos, chunk);
    fp->mem_pos += chunk;
    done += chunk;
    if(done < want)
      fp->eof = 1;
    return done / size;
  }

  while(done < want) {
    n = read(fp->fd, dst + done, (int)(want - done));
    if(n < 0) {
      fp->err = 1;
      break;
    }
    if(n == 0) {
      fp->eof = 1;
      break;
    }
    done += (size_t)n;
  }

  return done / size;
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

  if(file_can_buffer_output(fp)) {
    const char *src;
    size_t done;

    src = (const char*)ptr;
    done = 0;
    while(done < want) {
      if(file_write_byte(fp, src[done]) == EOF)
        break;
      done++;
    }
    return done / size;
  }

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

int
vsscanf(const char *s, const char *fmt, va_list ap)
{
  const char *input;
  const char *f;
  int assigned;
  int input_fail;

  if(s == 0 || fmt == 0)
    return EOF;

  input = s;
  f = fmt;
  assigned = 0;
  input_fail = 0;

  while(*f) {
    if(isspace((uchar)*f)) {
      while(isspace((uchar)*f))
        f++;
      while(isspace((uchar)*input))
        input++;
      continue;
    }

    if(*f != '%') {
      if(*input == '\0') {
        input_fail = 1;
        break;
      }
      if(*input != *f)
        break;
      input++;
      f++;
      continue;
    }

    f++;
    if(*f == '%') {
      if(*input == '\0') {
        input_fail = 1;
        break;
      }
      if(*input != '%')
        break;
      input++;
      f++;
      continue;
    }

    {
      int suppress;
      int width;
      int len;
      int spec;

      suppress = 0;
      width = -1;
      len = SCAN_LEN_DEFAULT;

      if(*f == '*') {
        suppress = 1;
        f++;
      }

      if(isdigit((uchar)*f)) {
        width = 0;
        while(isdigit((uchar)*f)) {
          width = width * 10 + (*f - '0');
          f++;
        }
      }

      if(*f == 'h') {
        len = SCAN_LEN_SHORT;
        f++;
        if(*f == 'h') {
          len = SCAN_LEN_CHAR;
          f++;
        }
      } else if(*f == 'l') {
        len = SCAN_LEN_LONG;
        f++;
        if(*f == 'l') {
          len = SCAN_LEN_LLONG;
          f++;
        }
      } else if(*f == 'j') {
        len = SCAN_LEN_INTMAX;
        f++;
      } else if(*f == 'z') {
        len = SCAN_LEN_SIZE;
        f++;
      } else if(*f == 't') {
        len = SCAN_LEN_PTRDIFF;
        f++;
      }

      spec = (uchar)*f;
      if(spec == '\0')
        break;
      f++;

      switch(spec) {
      case 'd':
      case 'i':
      case 'o':
      case 'u':
      case 'x':
      case 'X':
      case 'p': {
        int consumed;
        int rc;
        int base;
        int is_signed;
        long long sval;
        unsigned long long uval;

        sval = 0;
        uval = 0;

        while(isspace((uchar)*input))
          input++;
        if(*input == '\0') {
          input_fail = 1;
          goto scan_done;
        }

        is_signed = (spec == 'd' || spec == 'i');
        if(spec == 'd')
          base = 10;
        else if(spec == 'i')
          base = 0;
        else if(spec == 'o')
          base = 8;
        else if(spec == 'u')
          base = 10;
        else
          base = 16;

        rc = scan_parse_integer(input, width, base, is_signed,
                                &consumed, &sval, &uval);
        if(rc <= 0)
          goto scan_done;

        if(!suppress) {
          if(spec == 'p')
            *va_arg(ap, void**) = (void*)(uintptr_t)uval;
          else if(is_signed)
            scan_store_signed(&ap, len, sval);
          else
            scan_store_unsigned(&ap, len, uval);
          assigned++;
        }

        input += consumed;
        break;
      }

      case 'c': {
        char *out;
        int count;
        int i;

        if(len != SCAN_LEN_DEFAULT)
          goto scan_done;

        count = (width < 0) ? 1 : width;
        if(count == 0)
          goto scan_done;
        if(*input == '\0') {
          input_fail = 1;
          goto scan_done;
        }

        out = suppress ? 0 : va_arg(ap, char*);
        for(i = 0; i < count; i++) {
          if(input[i] == '\0') {
            input_fail = 1;
            goto scan_done;
          }
          if(out)
            out[i] = input[i];
        }

        input += count;
        if(!suppress)
          assigned++;
        break;
      }

      case 's': {
        char *out;
        int count;

        if(len != SCAN_LEN_DEFAULT)
          goto scan_done;

        while(isspace((uchar)*input))
          input++;
        if(*input == '\0') {
          input_fail = 1;
          goto scan_done;
        }

        out = suppress ? 0 : va_arg(ap, char*);
        count = 0;
        while(*input != '\0' && !isspace((uchar)*input) &&
              (width < 0 || count < width)) {
          if(out)
            out[count] = *input;
          input++;
          count++;
        }

        if(count == 0)
          goto scan_done;
        if(out)
          out[count] = '\0';
        if(!suppress)
          assigned++;
        break;
      }

      case '[': {
        const char *set_start;
        const char *set_end;
        char *out;
        int count;

        if(len != SCAN_LEN_DEFAULT)
          goto scan_done;

        set_start = f;
        if(*f == '^')
          f++;
        if(*f == ']')
          f++;
        while(*f != '\0' && *f != ']')
          f++;
        if(*f != ']')
          goto scan_done;

        set_end = f;
        f++;
        out = suppress ? 0 : va_arg(ap, char*);
        count = 0;

        while(*input != '\0' && (width < 0 || count < width) &&
              scan_scanset_match(set_start, set_end, (uchar)*input)) {
          if(out)
            out[count] = *input;
          input++;
          count++;
        }

        if(count == 0) {
          if(*input == '\0')
            input_fail = 1;
          goto scan_done;
        }
        if(out)
          out[count] = '\0';
        if(!suppress)
          assigned++;
        break;
      }

      case 'n':
        if(!suppress)
          scan_store_count(&ap, len, (size_t)(input - s));
        break;

      default:
        goto scan_done;
      }
    }
  }

scan_done:
  if(assigned == 0 && input_fail)
    return EOF;
  return assigned;
}

int
sscanf(const char *s, const char *fmt, ...)
{
  va_list ap;
  int n;

  va_start(ap, fmt);
  n = vsscanf(s, fmt, ap);
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
