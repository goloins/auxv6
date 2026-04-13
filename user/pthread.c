#include "pthread.h"

/*
 * pthread.c — single-threaded stub implementations of the pthread API.
 *
 * auxv6 is a single-threaded kernel; there is no real thread support.
 * These stubs satisfy the linker when ported code links against pthread
 * symbols. All operations that would affect multi-thread coordination
 * are no-ops or trivially correct for a single-thread environment.
 */

int
pthread_once(pthread_once_t *once_control, void (*init_routine)(void))
{
    if (*once_control == PTHREAD_ONCE_INIT) {
        *once_control = 1;
        init_routine();
    }
    return 0;
}

pthread_t
pthread_self(void)
{
    return (pthread_t)1;
}

int
pthread_equal(pthread_t t1, pthread_t t2)
{
    return t1 == t2;
}

int
pthread_mutex_init(pthread_mutex_t *mutex, const pthread_mutexattr_t *attr)
{
    (void)attr;
    *mutex = PTHREAD_MUTEX_INITIALIZER;
    return 0;
}

int
pthread_mutex_lock(pthread_mutex_t *mutex)
{
    (void)mutex;
    return 0;
}

int
pthread_mutex_unlock(pthread_mutex_t *mutex)
{
    (void)mutex;
    return 0;
}

int
pthread_mutex_destroy(pthread_mutex_t *mutex)
{
    (void)mutex;
    return 0;
}

/*
 * pthread_atfork — register fork handlers.
 *
 * auxv6 is single-threaded so fork handlers are not needed to protect
 * mutex state across fork. We register nothing and return success.
 */
int
pthread_atfork(void (*prepare)(void), void (*parent)(void), void (*child)(void))
{
    (void)prepare;
    (void)parent;
    (void)child;
    return 0;
}
