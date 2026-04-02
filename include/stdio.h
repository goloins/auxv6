#ifndef AUXV6_STDIO_H
#define AUXV6_STDIO_H

#include "stddef.h"
#include "sys/types.h"
#include "stdarg.h"

#ifdef printf
#undef printf
#endif

#ifndef EOF
#define EOF (-1)
#endif

#ifndef BUFSIZ
#define BUFSIZ 512
#endif /* AUXV6_STDIO_H */

#define _IOFBF 0
#define _IOLBF 1
#define _IONBF 2

typedef struct __auxv6_FILE FILE;
typedef off_t fpos_t;

extern FILE *stdin;
extern FILE *stdout;
extern FILE *stderr;

FILE *fopen(const char *path, const char *mode);
FILE *fdopen(int fd, const char *mode);
FILE *fmemopen(void *buf, size_t size, const char *mode);
int fclose(FILE *fp);
int fflush(FILE *fp);
int setvbuf(FILE *fp, char *buf, int mode, size_t size);
void setbuf(FILE *fp, char *buf);
void setlinebuf(FILE *fp);
int fseek(FILE *fp, long offset, int whence);
int fseeko(FILE *fp, off_t offset, int whence);
long ftell(FILE *fp);
off_t ftello(FILE *fp);
void rewind(FILE *fp);
int fgetpos(FILE *fp, fpos_t *pos);
int fsetpos(FILE *fp, const fpos_t *pos);

int ferror(FILE *fp);
int feof(FILE *fp);
void clearerr(FILE *fp);

int fgetc(FILE *fp);
int getc(FILE *fp);
int ungetc(int c, FILE *fp);
int fputc(int c, FILE *fp);
int putc(int c, FILE *fp);

char *fgets(char *s, int size, FILE *fp);
int fputs(const char *s, FILE *fp);
int puts(const char *s);

size_t fread(void *ptr, size_t size, size_t nmemb, FILE *fp);
size_t fwrite(const void *ptr, size_t size, size_t nmemb, FILE *fp);

int vdprintf(int fd, const char *fmt, va_list ap);
int dprintf(int fd, const char *fmt, ...);
int vprintf(const char *fmt, va_list ap);
int printf(const char *fmt, ...);
int vfprintf(FILE *fp, const char *fmt, va_list ap);
int fprintf(FILE *fp, const char *fmt, ...);
int vsnprintf(char *buf, size_t size, const char *fmt, va_list ap);
int snprintf(char *buf, size_t size, const char *fmt, ...);
int sprintf(char *buf, const char *fmt, ...);
int vsprintf(char *buf, const char *fmt, va_list ap);
int vsscanf(const char *s, const char *fmt, va_list ap);
int sscanf(const char *s, const char *fmt, ...);

void perror(const char *s);

ssize_t getdelim(char **lineptr, size_t *n, int delim, FILE *fp);
ssize_t getline(char **lineptr, size_t *n, FILE *fp);

#define fileno(fp) ((fp) ? ((fp)->fd) : -1)

struct __auxv6_FILE {
  int fd;
  int flags;
  int err;
  int eof;
  int ungot;
  int has_ungot;
  int is_mem;
  int own_mem;
  const char *mem;
  size_t mem_size;
  size_t mem_pos;
  char *buf;
  size_t buf_size;
  size_t buf_len;
  int buf_mode;
  int buf_owned;
};

#endif
