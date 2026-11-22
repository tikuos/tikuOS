/*
 * Tiku Operating System v0.06
 * Simple. Persistent. Intelligent.
 * http://tiku-os.org
 *
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 *
 * tiku_posix_pthread.c - the C library's pthread names over the handles of
 * tiku_posix_thread.c: newlib's pthread_t, pthread_mutex_t and
 * pthread_cond_t are 32-bit handles, and its attribute records are read here.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include <errno.h>
#include <pthread.h>
#include <string.h>
#include "tiku_posix.h"

_Static_assert(sizeof(pthread_t) == sizeof(uint32_t), "a pthread_t is a 32-bit handle");
_Static_assert(sizeof(pthread_mutex_t) == sizeof(uint32_t), "a mutex is a 32-bit handle");
_Static_assert(sizeof(pthread_cond_t) == sizeof(uint32_t), "a condition is a 32-bit handle");

int pthread_attr_init(pthread_attr_t *attr)
{
    if (attr == NULL) return EINVAL;
    memset(attr, 0, sizeof *attr);
    attr->is_initialized = 1;
    attr->detachstate = PTHREAD_CREATE_JOINABLE;
    return 0;
}

int pthread_attr_destroy(pthread_attr_t *attr)
{
    if (attr == NULL) return EINVAL;
    attr->is_initialized = 0;
    return 0;
}

int pthread_attr_setdetachstate(pthread_attr_t *attr, int state)
{
    if (attr == NULL || (state != PTHREAD_CREATE_DETACHED && state != PTHREAD_CREATE_JOINABLE))
        return EINVAL;
    attr->detachstate = state;
    return 0;
}

int pthread_attr_getdetachstate(const pthread_attr_t *attr, int *state)
{
    if (attr == NULL || state == NULL) return EINVAL;
    *state = attr->detachstate;
    return 0;
}

int pthread_attr_setstacksize(pthread_attr_t *attr, size_t bytes)
{
    if (attr == NULL || bytes > 0x7fffffffu) return EINVAL;
    attr->stacksize = (int)bytes;
    return 0;
}

int pthread_attr_getstacksize(const pthread_attr_t *attr, size_t *bytes)
{
    if (attr == NULL || bytes == NULL) return EINVAL;
    *bytes = (size_t)attr->stacksize;
    return 0;
}

int pthread_create(pthread_t *thread, const pthread_attr_t *attr,
                   void *(*fn)(void *), void *arg)
{
    int used = attr != NULL && attr->is_initialized;

    return tiku_posix_thread_create((uint32_t *)thread,
                                    used ? (size_t)attr->stacksize : 0u,
                                    used && attr->detachstate == PTHREAD_CREATE_DETACHED,
                                    fn, arg);
}

int pthread_join(pthread_t thread, void **result) { return tiku_posix_thread_join(thread, result); }
int pthread_detach(pthread_t thread) { return tiku_posix_thread_detach(thread); }
void pthread_exit(void *result) { tiku_posix_thread_exit(result); for (;;) { } }
pthread_t pthread_self(void) { return (pthread_t)tiku_posix_self(); }
int pthread_equal(pthread_t a, pthread_t b) { return a == b; }

int pthread_mutexattr_init(pthread_mutexattr_t *attr)
{
    if (attr == NULL) return EINVAL;
    memset(attr, 0, sizeof *attr);
    attr->is_initialized = 1;
    attr->type = PTHREAD_MUTEX_DEFAULT;
    return 0;
}

int pthread_mutexattr_destroy(pthread_mutexattr_t *attr)
{
    if (attr == NULL) return EINVAL;
    attr->is_initialized = 0;
    return 0;
}

int pthread_mutexattr_settype(pthread_mutexattr_t *attr, int type)
{
    if (attr == NULL || type < PTHREAD_MUTEX_NORMAL || type > PTHREAD_MUTEX_DEFAULT) return EINVAL;
    attr->type = type;
    return 0;
}

int pthread_mutexattr_gettype(const pthread_mutexattr_t *attr, int *type)
{
    if (attr == NULL || type == NULL) return EINVAL;
    *type = attr->type;
    return 0;
}

int pthread_mutex_init(pthread_mutex_t *mutex, const pthread_mutexattr_t *attr)
{
    int type = TIKU_POSIX_MUTEX_NORMAL;

    if (attr != NULL && attr->is_initialized) {
        if (attr->type == PTHREAD_MUTEX_RECURSIVE) type = TIKU_POSIX_MUTEX_RECURSIVE;
        else if (attr->type == PTHREAD_MUTEX_ERRORCHECK) type = TIKU_POSIX_MUTEX_ERRORCHECK;
    }
    return tiku_posix_mutex_init((uint32_t *)mutex, type);
}

int pthread_mutex_destroy(pthread_mutex_t *mutex) { return tiku_posix_mutex_destroy((uint32_t *)mutex); }
int pthread_mutex_lock(pthread_mutex_t *mutex) { return tiku_posix_mutex_lock((uint32_t *)mutex, 0); }
int pthread_mutex_trylock(pthread_mutex_t *mutex) { return tiku_posix_mutex_lock((uint32_t *)mutex, 1); }
int pthread_mutex_unlock(pthread_mutex_t *mutex) { return tiku_posix_mutex_unlock((uint32_t *)mutex); }

int pthread_cond_init(pthread_cond_t *cond, const pthread_condattr_t *attr)
{
    (void)attr;
    return tiku_posix_cond_init((uint32_t *)cond);
}

int pthread_cond_destroy(pthread_cond_t *cond) { return tiku_posix_cond_destroy((uint32_t *)cond); }
int pthread_cond_wait(pthread_cond_t *cond, pthread_mutex_t *mutex)
{
    return tiku_posix_cond_wait((uint32_t *)cond, (uint32_t *)mutex);
}
int pthread_cond_signal(pthread_cond_t *cond) { return tiku_posix_cond_signal((uint32_t *)cond, 0); }
int pthread_cond_broadcast(pthread_cond_t *cond) { return tiku_posix_cond_signal((uint32_t *)cond, 1); }
