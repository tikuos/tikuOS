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
