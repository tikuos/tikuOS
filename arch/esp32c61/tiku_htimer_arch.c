/*
 * Tiku Operating System v0.06
 * Simple. Ubiquitous. Intelligence, Everywhere.
 * http://tiku-os.org
 *
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 *
 * tiku_htimer_arch.c - ESP32-C61 high-resolution timer on SYSTIMER alarm 1.
 *
 * The tick's counter gives the time and a second alarm gives the interrupt,
 * on a line of its own so a critical section can keep one without the other.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include <kernel/timers/tiku_htimer.h>

#include "tiku_htimer_config.h"
#include "tiku_timer_arch.h"
#include "tiku_irq_arch.h"
#include "tiku_cpu_freq_boot_arch.h"
#include "tiku_esp32c61_regs.h"

/* 16 MHz over 1 MHz: one microsecond is 16 counts. */
#define COUNTS_PER_US   (ESP32C61_SYSTIMER_HZ / TIKU_HTIMER_ARCH_SECOND)

/** @brief Alarm interrupts taken with an alarm outstanding; main_minimal.c's
 *         htimer check reads it. */
volatile uint32_t tiku_htimer_arch_isr_count;

/** @brief Whether an alarm is outstanding. */
static volatile uint8_t ht_armed;

/** @brief Alarm 1: disarm first, so a rescheduling callback arms afresh. */
static void htimer_isr(void) {
    tiku_esp32c61_alarm_disarm(TIKU_ESP32C61_ALARM_HTIMER);
    if (ht_armed) {
        ht_armed = 0U;
        tiku_htimer_arch_isr_count++;
#ifndef TIKU_MINIMAL
        tiku_htimer_run_next();
#endif
    }
}

void tiku_htimer_arch_init(void) {
    ht_armed = 0U;
    tiku_esp32c61_alarm_disarm(TIKU_ESP32C61_ALARM_HTIMER);
    tiku_esp32c61_irq_attach(TIKU_ESP32C61_LINE_HTIMER,
                             ESP32C61_SRC_SYSTIMER(TIKU_ESP32C61_ALARM_HTIMER),
                             TIKU_ESP32C61_LEVEL_TIMER, htimer_isr);
    tiku_esp32c61_irq_enable(TIKU_ESP32C61_LINE_HTIMER);
}

tiku_htimer_clock_t tiku_htimer_arch_now(void) {
    return (tiku_htimer_clock_t)(tiku_cpu_esp32c61_systimer() / COUNTS_PER_US);
}

void tiku_htimer_arch_schedule(tiku_htimer_clock_t t) {
    uint64_t now = tiku_cpu_esp32c61_systimer();
    uint64_t us  = now / COUNTS_PER_US;
    /* The kernel's clock is 16-bit microseconds: recover the signed delta and
     * project it onto the counter.  One already due fires at once, since an
     * alarm armed in the past raises its interrupt straight away. */
    int16_t delta = (int16_t)((uint16_t)t - (uint16_t)us);
    uint64_t at = (delta > 0) ? (us + (uint64_t)delta) * COUNTS_PER_US : now;

    ht_armed = 1U;
    tiku_esp32c61_alarm_arm(TIKU_ESP32C61_ALARM_HTIMER, at);
}
