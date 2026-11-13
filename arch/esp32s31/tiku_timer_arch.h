/*
 * Tiku Operating System v0.06
 * Simple. Persistent. Intelligent.
 * http://tiku-os.org
 *
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 *
 * tiku_timer_arch.h - S31 128 Hz kernel clock and 1 MHz hardware timer.
 *
 * SPDX-License-Identifier: Apache-2.0
 */
#ifndef TIKU_ESP32S31_TIMER_ARCH_H_
#define TIKU_ESP32S31_TIMER_ARCH_H_
#include <hal/tiku_clock_hal.h>
#define TIKU_CLOCK_ARCH_SECOND 128u
#define TIKU_HTIMER_ARCH_SECOND 1000000UL
#define TIKU_CLOCK_ARCH_MS_TO_TICKS(ms) \
    (((unsigned long)(ms) * 128u + 999u) / 1000u)
#define TIKU_CLOCK_ARCH_TICKS_TO_MS(ticks) \
    ((unsigned long)(ticks) * 1000u / 128u)
#endif
