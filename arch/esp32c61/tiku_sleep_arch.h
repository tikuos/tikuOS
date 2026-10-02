/*
 * Tiku Operating System v0.06
 * Simple. Ubiquitous. Intelligence, Everywhere.
 * http://tiku-os.org
 *
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 *
 * tiku_sleep_arch.h - ESP32-C61 deep sleep and the LP timer it wakes on.
 *
 * Deep sleep powers the HP domain down -- core, SRAM, the flash's supply --
 * leaving the LP timer and the PMU running.  Waking is a reset: the boot after
 * it restores durable state from the flash mirror, as after any other.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#ifndef TIKU_ESP32C61_SLEEP_ARCH_H_
#define TIKU_ESP32C61_SLEEP_ARCH_H_

#include <stdint.h>

/** @brief The LP timer's count: it runs through deep sleep and resets. */
uint64_t tiku_esp32c61_lp_ticks(void);

/** @brief The LP timer's rate, measured once against SYSTIMER. */
uint32_t tiku_esp32c61_lp_hz(void);

/**
 * @brief Checkpoint durable state, then sleep until @p us have passed.
 *
 * Does not return: the wake is a reset, and 0 arms no timer, so only the
 * reset line wakes it.  A sleep that never takes the core, or a timed one
 * whose wake never comes, resets the part a second late instead -- the
 * latter through the LP watchdog -- and tiku_esp32c61_sleep_missed() says so.
 */
void tiku_esp32c61_deep_sleep(uint64_t us) __attribute__((noreturn));

/** @brief Light sleep may also end on GPIO9 held low (the BOOT pin). */
#define TIKU_ESP32C61_NAP_PIN   (1U << 0)

/**
 * @brief Light sleep: the core stalls and the PLL stops, while the crystal,
 *        UART0 and SYSTIMER run on -- RAM, time and console input survive.
 *
 * Ends after @p us (0: no timer), on a byte arriving at UART0, which is kept,
 * or with @p flags TIKU_ESP32C61_NAP_PIN on GPIO9 low.  Returns the PMU's
 * wake cause (ESP32C61_PMU_WAKE_*), 0 when a waiting wake refused the sleep.
 */
uint32_t tiku_esp32c61_light_sleep(uint64_t us, unsigned flags);

/**
 * @brief The idle hook for `sleep lpm3`: light sleep until the next SYSTIMER
 *        deadline, or wfi when that is too near to pay for the way in.
 *
 * Only timers and console bytes end it.  Other interrupts wait for the next
 * deadline, which is why it is chosen, never the default.
 */
void tiku_esp32c61_light_idle(void);

/** @brief Keep the light-sleep idle to wfi while held (the radio's PLL
 *         clocks), counted.  @param on  Non-zero to take, 0 to give back */
void tiku_esp32c61_sleep_hold(int on);

/** @brief Latch why this boot began; once, early in every boot. */
void tiku_esp32c61_sleep_boot(void);

/** @brief PMU wake-cause bits (ESP32C61_PMU_WAKE_*) after a deep-sleep wake,
 *         else 0. */
uint32_t tiku_esp32c61_wake_cause(void);

/** @brief Nonzero when the last deep sleep fell through to a reset. */
int tiku_esp32c61_sleep_missed(void);

/** @brief The LP timer as this boot began: after a wake, when it woke. */
uint64_t tiku_esp32c61_boot_lp_ticks(void);

#endif /* TIKU_ESP32C61_SLEEP_ARCH_H_ */
