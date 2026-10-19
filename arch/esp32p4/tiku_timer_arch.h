/*
 * Tiku Operating System v0.06
 * Simple. Persistent. Intelligent.
 * http://tiku-os.org
 *
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 *
 * tiku_timer_arch.h - P4 kernel timebase configuration.
 *
 * The kernel clock ticks at 128 Hz and the htimer at 1 MHz; millisecond to
 * tick conversion rounds up.  Also declares tiku_p4_fatal().
 *
 * SPDX-License-Identifier: Apache-2.0
 */
#ifndef TIKU_ESP32P4_TIMER_ARCH_H_
#define TIKU_ESP32P4_TIMER_ARCH_H_
#include <hal/tiku_clock_hal.h>
#define TIKU_CLOCK_ARCH_SECOND 128u
#define TIKU_HTIMER_ARCH_SECOND 1000000UL
#define TIKU_CLOCK_ARCH_MS_TO_TICKS(ms) \
    (((unsigned long)(ms) * 128u + 999u) / 1000u)
#define TIKU_CLOCK_ARCH_TICKS_TO_MS(ticks) \
    ((unsigned long)(ticks) * 1000u / 128u)
/** @brief Halt after reporting a native kernel failure. */
void tiku_p4_fatal(const char *reason) __attribute__((noreturn));
#endif
