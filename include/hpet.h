#ifndef AUXV6_HPET_H
#define AUXV6_HPET_H

#include "types.h"

int                hpet_init(void);
int                hpet_available(void);
unsigned long long hpet_read_counter(void);
uint               hpet_period_fs(void);
uint               hpet_num_timers(void);
int                hpet_counter_is_64bit(void);
void               hpet_stop(void);

#endif
