#ifndef AUXV6_HPET_H
#define AUXV6_HPET_H

#include "types.h"

int                hpet_init(void);
int                hpet_available(void);
int                hpet_start_periodic_test(uint freq_hz);
unsigned long long hpet_read_counter(void);
uint               hpet_period_fs(void);
uint               hpet_num_timers(void);
uint               hpet_irq_count(void);
int                hpet_irq_line(void);
int                hpet_test_enabled(void);
int                hpet_counter_is_64bit(void);
void               hpet_stop(void);

#endif
