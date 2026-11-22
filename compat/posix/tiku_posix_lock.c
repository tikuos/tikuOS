/*
 * Tiku Operating System v0.06
 * Simple. Persistent. Intelligent.
 * http://tiku-os.org
 *
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 *
 * tiku_posix_lock.c - newlib's retargetable locks, which guard its stdio
 * streams, environment, time zone and exit handlers, as recursive locks of
 * the threads group.  Defining every one of them keeps newlib's own
 * definitions, which do nothing, out of the link.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include <errno.h>
#include <stdint.h>
#include <string.h>
#include <sys/lock.h>
#include "tiku_posix.h"
#include <hal/tiku_cpu.h>

struct __lock {
    tiku_posix_lock_t lock;
};

/* The locks newlib names. */
struct __lock __lock___sfp_recursive_mutex;
struct __lock __lock___atexit_recursive_mutex;
struct __lock __lock___at_quick_exit_mutex;
struct __lock __lock___malloc_recursive_mutex;
struct __lock __lock___env_recursive_mutex;
struct __lock __lock___tz_mutex;
struct __lock __lock___dd_hash_mutex;
struct __lock __lock___arc4random_mutex;

/* The locks newlib makes, one for each stream it opens.  A lock made while
 * all are in use is the shared one: the streams behind it exclude each
 * other as well as their own users. */
static struct __lock made[TIKU_POSIX_LOCKS];
static uint8_t made_used[TIKU_POSIX_LOCKS];
static struct __lock shared;

static void make(_LOCK_T *lock)
{
    *lock = &shared;
    tiku_atomic_enter();
    for (unsigned i = 0; i < TIKU_POSIX_LOCKS; i++) {
        if (!made_used[i]) {
            made_used[i] = 1u;
            memset(&made[i], 0, sizeof made[i]);
            *lock = &made[i];
            break;
        }
    }
    tiku_atomic_exit();
}

static void unmake(_LOCK_T lock)
{
    if (lock >= &made[0] && lock < &made[TIKU_POSIX_LOCKS]) {
        tiku_atomic_enter();
        made_used[lock - &made[0]] = 0u;
        tiku_atomic_exit();
    }
}

/** @brief Take @p lock.  The kernel thread cannot wait for a worker that
 *         holds it, and running on would break what the lock guards, so
 *         that case traps. */
static void acquire(_LOCK_T lock)
{
    if (tiku_posix_lock_acquire(&lock->lock, 0) == EAGAIN) __builtin_trap();
}

void __retarget_lock_init(_LOCK_T *lock) { make(lock); }
void __retarget_lock_init_recursive(_LOCK_T *lock) { make(lock); }
void __retarget_lock_close(_LOCK_T lock) { unmake(lock); }
void __retarget_lock_close_recursive(_LOCK_T lock) { unmake(lock); }
void __retarget_lock_acquire(_LOCK_T lock) { acquire(lock); }
void __retarget_lock_acquire_recursive(_LOCK_T lock) { acquire(lock); }

/* Zero when taken, as ftrylockfile() returns. */
int __retarget_lock_try_acquire(_LOCK_T lock)
{
    return tiku_posix_lock_acquire(&lock->lock, 1);
}

int __retarget_lock_try_acquire_recursive(_LOCK_T lock)
{
    return tiku_posix_lock_acquire(&lock->lock, 1);
}

void __retarget_lock_release(_LOCK_T lock) { (void)tiku_posix_lock_release(&lock->lock); }
void __retarget_lock_release_recursive(_LOCK_T lock) { (void)tiku_posix_lock_release(&lock->lock); }
