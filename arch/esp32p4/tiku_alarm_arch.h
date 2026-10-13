/*
 * Tiku Operating System v0.06
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 * tiku_alarm_arch.h - P4 one-shot alarm on SYSTIMER comparator 1.
 * SPDX-License-Identifier: Apache-2.0
 */
#ifndef TIKU_ESP32P4_ALARM_ARCH_H_
#define TIKU_ESP32P4_ALARM_ARCH_H_
#include <stdint.h>
/** @brief Claim comparator 1 and IRQ line 1; return -1 for NULL or occupied.
 * @note SYSTIMER must be initialized. Callback runs with IRQs masked and may rearm.
 */
int tiku_p4_alarm_init(void (*callback)(void));
/** @brief Arm an absolute 52-bit SYSTIMER deadline less than half a wrap ahead.
 * @note Late or nearer-than-4-microsecond deadlines are clamped to 4 microseconds.
 * @return Zero on success, -1 on invalid/unowned input or snapshot timeout.
 */
int tiku_p4_alarm_schedule(uint64_t deadline);
/** @brief Cancel a pending delivery without releasing the alarm owner. */
void tiku_p4_alarm_cancel(void);
/** @brief Cancel and release this alarm without touching a replacement IRQ owner. */
void tiku_p4_alarm_stop(void);
#endif
