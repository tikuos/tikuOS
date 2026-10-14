/*
 * Tiku Operating System v0.06
 * Simple. Persistent. Intelligent.
 * http://tiku-os.org
 *
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 *
 * tiku_sleep_arch.h - ESP32-C5 deep and light sleep, and the LP timer.
 *
 * Deep sleep powers the HP domain down (core, SRAM, the flash's supply) and
 * leaves the LP timer and the PMU running.  Its wake is a reset, and the boot
 * restores durable state from the flash mirror.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#ifndef TIKU_ESP32C5_SLEEP_ARCH_H_
#define TIKU_ESP32C5_SLEEP_ARCH_H_

#include <stdint.h>

/* PMU wake-cause bits (pmu_bit_defs.h at the IDF pin). */
#define TIKU_C5_WAKE_EXT1   (1UL << 1)
#define TIKU_C5_WAKE_GPIO   (1UL << 2)
#define TIKU_C5_WAKE_TIMER  (1UL << 4)
#define TIKU_C5_WAKE_UART0  (1UL << 6)

/** @brief The LP timer's count: it runs through deep sleep and resets. */
uint64_t tiku_c5_lp_ticks(void);

/** @brief The LP timer's rate, measured once against SYSTIMER. */
uint32_t tiku_c5_lp_hz(void);

/**
 * @brief Checkpoint durable state, then sleep until @p us have passed.
 *
 * Does not return: the wake is a reset, and 0 arms no timer, so only the
 * reset line wakes it.  The USB console drops while the part sleeps and
 * enumerates again on the wake.
 *
 * @note A sleep that never takes the core, or a timed one whose wake never
 *       comes, ends in a reset a second late (the latter through the LP
 *       watchdog), and tiku_c5_sleep_missed() reports it.
 */
void tiku_c5_deep_sleep(uint64_t us) __attribute__((noreturn));

/** @brief Light sleep may also end on GPIO28 held low (the BOOT button). */
#define TIKU_C5_NAP_PIN   (1U << 0)

/**
 * @brief Light sleep: the core stalls and the PLL stops; the crystal and
 *        SYSTIMER run on, so RAM and time survive.
 *
 * Ends after @p us (0: no timer), on a byte at UART0 when it is the console,
 * or, with TIKU_C5_NAP_PIN in @p flags, on GPIO28 low.
 *
 * @return The PMU's wake cause (TIKU_C5_WAKE_*), or 0 when a radio holds
 *         the PHY, a pending wake rejected the sleep or no wake came within
 *         @p us + 2 s
 */
uint32_t tiku_c5_light_sleep(uint64_t us, unsigned flags);

/**
 * @brief The idle hook for `sleep lpm3` and `sleep lpm4`: light sleep until
 *        the next SYSTIMER deadline.
 *
 * A deadline under 3 ms away, queued console bytes or a radio holding the
 * PHY leaves it in wfi.  Only the timers (and a UART0 console byte) end the
 * sleep; other interrupts wait for the next deadline.
 */
void tiku_c5_light_idle(void);

/**
 * @brief Latch this boot's LP time, the deep-sleep wake cause and the
 *        missed-sleep flag.
 *
 * @note Call once, early in every boot.
 */
void tiku_c5_sleep_boot(void);

/** @brief Light sleeps that ended on a wake since boot, in @p count, and
 *         the time they lasted in microseconds, in @p us. */
void tiku_c5_light_stats(uint32_t *count, uint64_t *us);

/** @brief PMU wake-cause bits (TIKU_C5_WAKE_*) after a deep-sleep wake,
 *         else 0. */
uint32_t tiku_c5_wake_cause(void);

/** @brief Nonzero when the last deep sleep fell through to a reset. */
int tiku_c5_sleep_missed(void);

/** @brief The LP timer as this boot began: after a wake, when it woke. */
uint64_t tiku_c5_boot_lp_ticks(void);

#endif /* TIKU_ESP32C5_SLEEP_ARCH_H_ */
