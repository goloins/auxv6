/*
 * <shadow.h> - shadow password database access
 */

#ifndef _SHADOW_H
#define _SHADOW_H

struct spwd {
  char *sp_namp;    /* login name */
  char *sp_pwdp;    /* encrypted password */
  long  sp_lstchg;  /* date of last change */
  long  sp_min;     /* min days between changes */
  long  sp_max;     /* max days between changes */
  long  sp_warn;    /* days before expiry to warn */
  long  sp_inact;   /* days after expiry until disabled */
  long  sp_expire;  /* account expiry date */
  unsigned long sp_flag;
};

struct spwd *getspnam(const char *name);

#endif /* _SHADOW_H */