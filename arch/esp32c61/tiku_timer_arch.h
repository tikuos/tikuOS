/*
 * Tiku Operating System v0.06
 * Simple. Ubiquitous. Intelligence, Everywhere.
 * http://tiku-os.org
 *
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 *
 * tiku_timer_arch.h - ESP32-C61 system tick on a SYSTIMER alarm.
 *
 * SYSTIMER unit 0 counts at 16 MHz from the crystal; alarm 0 raises the tick
 * at absolute counts, so the tick never drifts from the counter.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#ifndef TIKU_ESP32C61_TIMER_ARCH_H_
#define TIKU_ESP32C61_TIMER_ARCH_H_

#include <stdint.h>

#ifndef TIKU_CLOCK_ARCH_TIME_T_DEFINED
/** @brief A tick count. */
typedef unsigned long tiku_clock_arch_time_t;
#define TIKU_CLOCK_ARCH_TIME_T_DEFINED
#endif

/** @brief Ticks per second unless the build asks for another rate. */
#ifndef TIKU_CLOCK_ARCH_CONF_SECOND
#define TIKU_CLOCK_ARCH_CONF_SECOND 128
#endif

/** @brief Ticks per second; a build sets it through the CONF_ form. */
#define TIKU_CLOCK_ARCH_SECOND  TIKU_CLOCK_ARCH_CONF_SECOND

/** @brief SYSTIMER counts per tick: 125000 at 128 Hz, exact. */
#define TIKU_CLOCK_ARCH_INTERVAL (16000000UL / TIKU_CLOCK_ARCH_SECOND)

/* The alarms: the tick owns 0, the high-resolution timer 1, and 2 is a
 * driver's own (the radio's timers). */
#define TIKU_ESP32C61_ALARM_TICK    0U
#define TIKU_ESP32C61_ALARM_HTIMER  1U
#define TIKU_ESP32C61_ALARM_DRIVER  2U

/*---------------------------------------------------------------------------*/
/* HAL ENTRY POINTS                                                          */
/*---------------------------------------------------------------------------*/

/** @brief Start the tick and zero the tick and seconds counters. */
void                   tiku_clock_arch_init(void);

/** @brief Ticks since boot. @return Monotonic tick count */
tiku_clock_arch_time_t tiku_clock_arch_time(void);

/** @brief Seconds since boot. @return Monotonic second count */
unsigned long          tiku_clock_arch_seconds(void);

/** @brief Set the seconds counter. @param sec  New value */
void                   tiku_clock_arch_set_seconds(unsigned long sec);

/** @brief Wait @p t ticks: a duration, not a deadline. @param t  Ticks */
void                   tiku_clock_arch_wait(tiku_clock_arch_time_t t);

/** @brief Busy-wait for microseconds off SYSTIMER. @param us  Microseconds */
void                   tiku_clock_arch_delay(unsigned int us);

/** @brief Sub-tick position, in 8 MHz counts. @return 0..fine_max()-1 */
unsigned short         tiku_clock_arch_fine(void);

/** @brief Sub-tick range. @return One more than the maximum fine value */
int                    tiku_clock_arch_fine_max(void);

/** @brief The earliest count an armed SYSTIMER alarm waits for after
 *         @p now: @p now when one has fired untaken, ~0 if none. */
uint64_t               tiku_esp32c61_alarm_due(uint64_t now);

/** @brief Clock-source fault; the crystal has no fallback.
 *  @return TIKU_CLOCK_ARCH_FAULT_NONE */
unsigned char          tiku_clock_arch_fault(void);

/** @brief Milliseconds to ticks, rounded up so a wait is never short. */
#define TIKU_CLOCK_ARCH_MS_TO_TICKS(ms) \
    (((unsigned long)(ms) * TIKU_CLOCK_ARCH_SECOND + 999UL) / 1000UL)

/** @brief Ticks to milliseconds. */
#define TIKU_CLOCK_ARCH_TICKS_TO_MS(ticks) \
    (((unsigned long)(ticks) * 1000UL) / TIKU_CLOCK_ARCH_SECOND)

/*---------------------------------------------------------------------------*/
/* PORT INTERNALS                                                            */
/*---------------------------------------------------------------------------*/

/** @brief Arm SYSTIMER alarm @p n for absolute count @p at; a past one
 *         fires at once. */
void tiku_esp32c61_alarm_arm(unsigned n, uint64_t at);

/** @brief Disarm alarm @p n and clear its pending interrupt. */
void tiku_esp32c61_alarm_disarm(unsigned n);

/** @brief Whether the tick interrupt is running. @return 1 once started */
int  tiku_esp32c61_clock_running(void);

#endif /* TIKU_ESP32C61_TIMER_ARCH_H_ */
