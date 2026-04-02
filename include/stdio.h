#ifndef AUXV6_STDIO_H
#define AUXV6_STDIO_H

#include "stddef.h"
#include "sys/types.h"
#include "stdarg.h"

#ifndef EOF
#define EOF (-1)
#endif

#ifndef BUFSIZ
#define BUFSIZ 512
#endif /* AUXV6_STDIO_H */

typedef struct __auxv6_FILE FILE;

extern FILE *stdin;
extern FILE *stdout;
extern FILE *stderr;

FILE *fopen(const char *path, const char *mode);
FILE *fdopen(int fd, const char *mode);
FILE *fmemopen(void *buf, size_t size, const char *mode);
int fclose(FILE *fp);
int fflush(FILE *fp);

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

int vfprintf(FILE *fp, const char *fmt, va_list ap);
int fprintf(FILE *fp, const char *fmt, ...);
int vsnprintf(char *buf, size_t size, const char *fmt, va_list ap);
int snprintf(char *buf, size_t size, const char *fmt, ...);
int sprintf(char *buf, const char *fmt, ...);
int vsprintf(char *buf, const char *fmt, va_list ap);

void perror(const char *s);

ssize_t getdelim(char **lineptr, size_t *n, int delim, FILE *fp);
ssize_t getline(char **lineptr, size_t *n, FILE *fp);

/* Keep auxv6 native printf(fd, ...) intact and provide stdio-style macros. */
#define vprintf(fmt, ap) vfprintf(stdout, (fmt), (ap))
#define printf(...) fprintf(stdout, __VA_ARGS__)

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
};

#endif
