#include "types.h"
#include "date.h"
#include "auxv6/user.h"

int
main(int argc, char *argv[])
{
  struct rtcdate r;

  if(argc != 1){
    printf(2, "usage: date\n");
    exit();
  }

  if(date(&r) < 0){
    printf(2, "date: failed to read RTC\n");
    exit();
  }

  printf(1, "%d-%02d-%02d %02d:%02d:%02d UTC\n",
         r.year, r.month, r.day, r.hour, r.minute, r.second);
  exit();
}
