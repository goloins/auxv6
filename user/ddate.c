#include "types.h"
#include "stdio.h"
#include "stdlib.h"
#include "string.h"
#include "time.h"
#include "auxv6/user.h"

static const char *const discordian_weekdays[5] = {
  "Sweetmorn", "Boomtime", "Pungenday", "Prickle-Prickle", "Setting Orange"
};

static const char *const discordian_seasons[5] = {
  "Chaos", "Discord", "Confusion", "Bureaucracy", "The Aftermath"
};

static int
is_leap_year(int year)
{
  if((year % 400) == 0)
    return 1;
  if((year % 100) == 0)
    return 0;
  return (year % 4) == 0;
}

static const char *
ordinal_suffix(int day)
{
  int mod100;

  mod100 = day % 100;
  if(mod100 >= 11 && mod100 <= 13)
    return "th";

  switch(day % 10) {
  case 1:
    return "st";
  case 2:
    return "nd";
  case 3:
    return "rd";
  default:
    return "th";
  }
}

int
main(int argc, char *argv[])
{
  time_t now;
  struct tm local_tm;
  int yday;
  int yold;
  int season;
  int day_in_season;
  int weekday;

  if(argc > 1) {
    dprintf(2, "usage: ddate\n");
    exit(1);
  }

  now = time(0);
  if(localtime_r(&now, &local_tm) == 0) {
    dprintf(2, "ddate: failed to read local time\n");
    exit(1);
  }

  yday = local_tm.tm_yday;
  if(is_leap_year(local_tm.tm_year + 1900)) {
    if(local_tm.tm_mon == 1 && local_tm.tm_mday == 29) {
      dprintf(1, "St. Tib's Day, in the YOLD %d\n", local_tm.tm_year + 1900 + 1166);
      exit(0);
    }
    if(yday > 59)
      yday--;
  }

  yold = local_tm.tm_year + 1900 + 1166;
  season = yday / 73;
  day_in_season = (yday % 73) + 1;
  weekday = yday % 5;

  dprintf(1,
          "%s, the %d%s day of %s in the YOLD %d\n",
          discordian_weekdays[weekday],
          day_in_season,
          ordinal_suffix(day_in_season),
          discordian_seasons[season],
          yold);
  exit(0);
}
