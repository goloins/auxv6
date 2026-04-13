#ifndef _PTHREAD_H
#define _PTHREAD_H

#include "sys/types.h"

#define PTHREAD_ONCE_INIT 0U
#define PTHREAD_MUTEX_INITIALIZER 0U

int pthread_once(pthread_once_t *once_control, void (*init_routine)(void));
pthread_t pthread_self(void);
int pthread_equal(pthread_t t1, pthread_t t2);
int pthread_mutex_init(pthread_mutex_t *mutex, const pthread_mutexattr_t *attr);
int pthread_mutex_lock(pthread_mutex_t *mutex);
int pthread_mutex_unlock(pthread_mutex_t *mutex);
int pthread_mutex_destroy(pthread_mutex_t *mutex);
int pthread_atfork(void (*prepare)(void), void (*parent)(void), void (*child)(void));

#endif
