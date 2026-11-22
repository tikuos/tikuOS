/*
 * Tiku Operating System v0.06
 * Simple. Persistent. Intelligent.
 * http://tiku-os.org
 *
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 *
 * tiku_posix_time.c - clock_gettime, clock_getres and gettimeofday over the
 * kernel's clocks.
 *
 * The monotonic clocks read tiku_clock_usec(), microseconds since the clock
 * started.  The real-time clocks read the kernel's wall clock, in whole
 * seconds, while it holds a time (tiku_rtc_read() not TIKU_RTC_UNSET);
 * while it does not, they read 1970-01-01 plus the time since start, a
 * moving value no reader takes for the date.  Whether a set time is current is the kernel's
 * tiku_rtc_state(), not a POSIX question.  None of these calls waits, so
 * the kernel thread and worker threads may both make them.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include <errno.h>
#include <stdint.h>
#include <sys/time.h>
#include <time.h>
#include "tiku.h"
#include <kernel/cpu/tiku_rtc.h>
#include <kernel/timers/tiku_clock.h>

#if !defined(TIKU_CLOCK_ARCH_USEC) || !TIKU_CLOCK_ARCH_USEC
#error "compat/posix time calls need the port's microsecond clock (TIKU_CLOCK_ARCH_USEC)"
#endif

int _gettimeofday(struct timeval *tv, void *tz);

/** @brief Whether @p id names a monotonic clock this library answers. */
static int monotonic(clockid_t id)
{
    if (id == CLOCK_MONOTONIC) return 1;
#ifdef CLOCK_MONOTONIC_RAW
    if (id == CLOCK_MONOTONIC_RAW) return 1;
#endif
#ifdef CLOCK_MONOTONIC_COARSE
    if (id == CLOCK_MONOTONIC_COARSE) return 1;
#endif
    return 0;
}

/** @brief Whether @p id names a real-time clock this library answers. */
static int realtime(clockid_t id)
{
    if (id == CLOCK_REALTIME) return 1;
#ifdef CLOCK_REALTIME_COARSE
    if (id == CLOCK_REALTIME_COARSE) return 1;
#endif
    return 0;
}

/**
 * @brief The time of clock @p id.
 * @return 0 with @p ts filled; -1 with EINVAL for a clock this library does
 *         not answer or a NULL @p ts, which is then not written.
 */
int clock_gettime(clockid_t id, struct timespec *ts)
{
    uint64_t us;
    uint32_t wall;

    if (ts == NULL || (!monotonic(id) && !realtime(id))) {
        errno = EINVAL;
        return -1;
    }
    if (realtime(id) && tiku_rtc_read(&wall) != TIKU_RTC_UNSET) {
        ts->tv_sec = (time_t)wall;
        ts->tv_nsec = 0;
        return 0;
    }
    us = tiku_clock_usec();
    ts->tv_sec = (time_t)(us / 1000000u);
    ts->tv_nsec = (long)(us % 1000000u) * 1000L;
    return 0;
}

/**
 * @brief The resolution of clock @p id: one microsecond for the monotonic
 *        clocks, one second for the real-time clocks, the wall clock's.
 * @return 0, with @p res filled when it is not NULL; -1 with EINVAL for a
 *         clock this library does not answer.
 */
int clock_getres(clockid_t id, struct timespec *res)
{
    if (!monotonic(id) && !realtime(id)) {
        errno = EINVAL;
        return -1;
    }
    if (res != NULL) {
        res->tv_sec = realtime(id) ? 1 : 0;
        res->tv_nsec = realtime(id) ? 0 : 1000L;
    }
    return 0;
}

/**
 * @brief newlib's hook behind gettimeofday() and time(): the real-time
 *        clock in microseconds.  @p tz is ignored.
 * @return 0; a NULL @p tv is not written.
 */
int _gettimeofday(struct timeval *tv, void *tz)
{
    struct timespec ts;

    (void)tz;
    if (tv == NULL) return 0;
    (void)clock_gettime(CLOCK_REALTIME, &ts);
    tv->tv_sec = ts.tv_sec;
    tv->tv_usec = (suseconds_t)(ts.tv_nsec / 1000L);
    return 0;
}
