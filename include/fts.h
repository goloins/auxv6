/*
 * <fts.h> - BSD file hierarchy traversal
 */

#ifndef _FTS_H
#define _FTS_H

#include "types.h"
#include "sys/types.h"
#include "stat.h"

typedef struct _ftsent {
  struct _ftsent *fts_cycle;
  struct _ftsent *fts_parent;
  struct _ftsent *fts_link;
  long            fts_number;
  void           *fts_pointer;
  char           *fts_accpath;
  char           *fts_path;
  int             fts_errno;
  int             fts_symfd;
  ushort          fts_pathlen;
  ushort          fts_namelen;
  ino_t           fts_ino;
  dev_t           fts_dev;
  nlink_t         fts_nlink;
  short           fts_level;
  short           fts_info;
  short           fts_flags;
  short           fts_instr;
  struct stat    *fts_statp;
  char            fts_name[1];
} FTSENT;

typedef struct _fts {
  FTSENT  **_ents;
  int       _count;
  int       _cap;
  int       _index;
  int       _options;
  FTSENT   *fts_cur;
  FTSENT   *fts_child;
  int     (*fts_compar)(const FTSENT **, const FTSENT **);
} FTS;

/* fts_open options */
#define FTS_COMFOLLOW 0x001
#define FTS_LOGICAL   0x002
#define FTS_NOCHDIR   0x004
#define FTS_PHYSICAL  0x008
#define FTS_SEEDOT    0x010
#define FTS_XDEV      0x020

/* fts_info values */
#define FTS_D       1
#define FTS_DC      2
#define FTS_DEFAULT 3
#define FTS_DNR     4
#define FTS_DOT     5
#define FTS_DP      6
#define FTS_ERR     7
#define FTS_F       8
#define FTS_NS      9
#define FTS_NSOK    10
#define FTS_SL      11
#define FTS_SLNONE  12

FTS    *fts_open(char * const *path_argv, int options,
                 int (*compar)(const FTSENT **, const FTSENT **));
FTSENT *fts_read(FTS *ftsp);
FTSENT *fts_children(FTS *ftsp, int instr);
int     fts_close(FTS *ftsp);

#endif /* _FTS_H */