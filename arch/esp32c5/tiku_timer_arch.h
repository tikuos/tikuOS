/*
 * Tiku Operating System v0.06
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 * tiku_timer_arch.h - C5 kernel tick and hardware-timer configuration.
 * SPDX-License-Identifier: Apache-2.0
 */
#ifndef TIKU_ESP32C5_TIMER_ARCH_H_
#define TIKU_ESP32C5_TIMER_ARCH_H_
#include <hal/tiku_clock_hal.h>
#define TIKU_CLOCK_ARCH_SECOND 128u
#define TIKU_HTIMER_ARCH_SECOND 1000000UL
#define TIKU_CLOCK_ARCH_MS_TO_TICKS(ms) (((unsigned long)(ms) * 128u + 999u) / 1000u)
#define TIKU_CLOCK_ARCH_TICKS_TO_MS(ticks) ((unsigned long)(ticks) * 1000u / 128u)
/** @brief Halt after reporting an unrecoverable peripheral failure. */
void tiku_c5_fatal(const char *reason) __attribute__((noreturn));
#endif
