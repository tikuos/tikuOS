/*
 * Tiku Operating System v0.06
 * Simple. Ubiquitous. Intelligence, Everywhere.
 * http://tiku-os.org
 *
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 *
 * tiku_sleep_arch.h - ESP32-C61 deep and light sleep, and the LP timer.
 *
 * Deep sleep powers the HP domain down (core, SRAM, the flash's supply) and
 * leaves the LP timer and the PMU running.  Its wake is a reset, and the boot
 * restores durable state from the flash mirror.
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
 * reset line wakes it.
 *
 * @note A sleep that never takes the core, or a timed one whose wake never
 *       comes, ends in a reset a second late (the latter through the LP
 *       watchdog), and tiku_esp32c61_sleep_missed() reports it.
 */
void tiku_esp32c61_deep_sleep(uint64_t us) __attribute__((noreturn));

/** @brief Light sleep may also end on GPIO9 held low (the BOOT pin). */
#define TIKU_ESP32C61_NAP_PIN   (1U << 0)

/**
 * @brief Light sleep: the core stalls and the PLL stops; the crystal, UART0
 *        and SYSTIMER run on, so RAM, time and console input survive.
 *
 * Ends after @p us (0: no timer), on a byte arriving at UART0, which is
 * kept, or, with TIKU_ESP32C61_NAP_PIN in @p flags, on GPIO9 low.
 *
 * @return The PMU's wake cause (ESP32C61_PMU_WAKE_*), or 0 when a pending
 *         wake rejected the sleep or no wake came within @p us + 2 s
 */
uint32_t tiku_esp32c61_light_sleep(uint64_t us, unsigned flags);

/**
 * @brief The idle hook for `sleep lpm3` and `sleep lpm4`: light sleep until
 *        the next SYSTIMER deadline.
 *
 * A deadline under 3 ms away, a received byte waiting, a DMA copy or a sleep
 * hold leaves it in wfi.  Only timers and console bytes end the sleep; other
 * interrupts wait for the next deadline.
 */
void tiku_esp32c61_light_idle(void);

/**
 * @brief Take (@p on non-zero) or give back a hold that keeps the light-sleep
 *        idle in wfi; holds are counted.
 *
 * The radio drivers hold it while they need the PLL clocks.
 */
void tiku_esp32c61_sleep_hold(int on);

/**
 * @brief Latch this boot's LP time, the deep-sleep wake cause and the
 *        missed-sleep flag.
 *
 * @note Call once, early in every boot.
 */
void tiku_esp32c61_sleep_boot(void);

/** @brief PMU wake-cause bits (ESP32C61_PMU_WAKE_*) after a deep-sleep wake,
 *         else 0. */
uint32_t tiku_esp32c61_wake_cause(void);

/** @brief Nonzero when the last deep sleep fell through to a reset. */
int tiku_esp32c61_sleep_missed(void);

/** @brief The LP timer as this boot began: after a wake, when it woke. */
uint64_t tiku_esp32c61_boot_lp_ticks(void);

#endif /* TIKU_ESP32C61_SLEEP_ARCH_H_ */
