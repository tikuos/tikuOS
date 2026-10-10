/*
 * Tiku Operating System v0.06
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 * tiku_systimer_arch.h - C5 16 MHz timebase and 128 Hz tick interrupts.
 * SPDX-License-Identifier: Apache-2.0
 */
#ifndef TIKU_ESP32C5_SYSTIMER_ARCH_H_
#define TIKU_ESP32C5_SYSTIMER_ARCH_H_
#include <stdint.h>

#define TIKU_C5_TICK_HZ     128u
#define TIKU_C5_TICK_COUNTS (16000000u / TIKU_C5_TICK_HZ)

/** @brief Initialize SYSTIMER and claim CLIC line 0; return -1 on failure.
 * @note Call after irq_init, before enabling machine interrupts.
 */
int tiku_c5_systimer_init(void);
/** @brief Read the 52-bit counter; leave *value unchanged on NULL or timeout.
 */
int tiku_c5_systimer_read(uint64_t *value);
/** @brief Stop the tick alarm and release its interrupt line. */
void tiku_c5_systimer_stop(void);
/** @brief Return elapsed 128 Hz ticks, modulo 2^32, including delayed ticks. */
uint32_t tiku_c5_systimer_ticks(void);
/** @brief Return serviced tick interrupts, modulo 2^32. */
uint32_t tiku_c5_systimer_interrupts(void);
/** @brief Return timer read failures since initialization. */
uint32_t tiku_c5_systimer_errors(void);
/** @brief Tick ISR notification; the kernel overrides the empty weak
 * definition. */
void tiku_c5_tick_notify(void);

#endif
