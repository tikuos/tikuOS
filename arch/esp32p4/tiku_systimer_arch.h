/*
 * Tiku Operating System v0.06
 * Simple. Persistent. Intelligent.
 * http://tiku-os.org
 *
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 *
 * tiku_systimer_arch.h - P4 16 MHz timebase and 128 Hz tick interrupts.
 *
 * Counter values are 52-bit SYSTIMER counts at 16 MHz and the tick,
 * interrupt and error counts wrap at 2^32.  The tick ISR calls
 * tiku_p4_tick_notify() after it rearms.
 *
 * SPDX-License-Identifier: Apache-2.0
 */
#ifndef TIKU_ESP32P4_SYSTIMER_ARCH_H_
#define TIKU_ESP32P4_SYSTIMER_ARCH_H_
#include <stdint.h>

#define TIKU_P4_TICK_HZ     128u
#define TIKU_P4_TICK_COUNTS (16000000u / TIKU_P4_TICK_HZ)

/** @brief Initialize SYSTIMER and claim CLIC line 0; return -1 on failure.
 * @note Call after irq_init, before enabling machine interrupts.
 */
int tiku_p4_systimer_init(void);
/** @brief Read the 52-bit counter; leave *value unchanged on NULL or timeout.
 */
int tiku_p4_systimer_read(uint64_t *value);
/** @brief Stop the tick alarm and release its interrupt line. */
void tiku_p4_systimer_stop(void);
/** @brief Return elapsed 128 Hz ticks, modulo 2^32, including delayed ticks. */
uint32_t tiku_p4_systimer_ticks(void);
/** @brief Return serviced tick interrupts, modulo 2^32. */
uint32_t tiku_p4_systimer_interrupts(void);
/** @brief Return timer read failures since initialization. */
uint32_t tiku_p4_systimer_errors(void);
/** @brief The nearest armed tick or hardware-timer deadline after @p now,
 *         @p now when one has fired untaken, or ~0 when none is armed. */
uint64_t tiku_p4_alarm_due(uint64_t now);
/** @brief Tick ISR notification; the kernel overrides the empty weak
 * definition. */
void tiku_p4_tick_notify(void);

#endif
