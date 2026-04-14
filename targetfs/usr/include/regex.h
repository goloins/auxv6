#ifndef _REGEX_H
#define _REGEX_H

#include "stddef.h"

typedef struct {
  int rm_so;
  int rm_eo;
} regmatch_t;

typedef struct {
  void *re_ptr;
  int cflags;
  char *pattern;
} regex_t;

/* cflags */
#define REG_EXTENDED 0x01
#define REG_ICASE    0x02
#define REG_NOSUB    0x04

/* regexec eflags */
#define REG_NOTBOL   0x01
#define REG_NOTEOL   0x02

/* return codes */
#define REG_OK       0
#define REG_NOMATCH  1
#define REG_BADPAT   2
#define REG_EESCAPE  3
#define REG_EPAREN   4
#define REG_EBRACK   5
#define REG_ESPACE   6

int regcomp(regex_t *preg, const char *regex, int cflags);
int regexec(const regex_t *preg, const char *string, size_t nmatch,
            regmatch_t pmatch[], int eflags);
size_t regerror(int errcode, const regex_t *preg, char *errbuf, size_t errbuf_size);
void regfree(regex_t *preg);

#endif
