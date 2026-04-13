#include "locale.h"
#include "string.h"
#include "errno.h"

static char loc_c[] = "C";

static struct lconv lc = {
  .decimal_point = ".",
  .thousands_sep = "",
  .grouping = "",
  .int_curr_symbol = "",
  .currency_symbol = "",
  .mon_decimal_point = "",
  .mon_thousands_sep = "",
  .mon_grouping = "",
  .positive_sign = "",
  .negative_sign = "",
  .int_frac_digits = 127,
  .frac_digits = 127,
  .p_cs_precedes = 127,
  .p_sep_by_space = 127,
  .n_cs_precedes = 127,
  .n_sep_by_space = 127,
  .p_sign_posn = 127,
  .n_sign_posn = 127,
};

char*
setlocale(int category, const char *locale)
{
  if(category < LC_ALL || category > LC_TIME) {
    errno = EINVAL;
    return 0;
  }

  if(locale == 0)
    return loc_c;

  if(strcmp(locale, "") == 0 || strcmp(locale, "C") == 0 ||
     strcmp(locale, "POSIX") == 0)
    return loc_c;

  errno = EINVAL;
  return 0;
}

struct lconv*
localeconv(void)
{
  return &lc;
}