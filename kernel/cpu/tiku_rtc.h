/*
 * Tiku Operating System v0.06
 * Simple. Ubiquitous. Intelligence, Everywhere.
 * http://tiku-os.org
 *
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 *
 * tiku_rtc.h - wall-clock RTC API.
 *
 * A soft RTC over tiku_clock_seconds() plus a durable epoch baseline, so the
 * last set time survives a reset and a power cycle, except in the host test
 * build.  One-second resolution; backs /sys/time.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#ifndef TIKU_RTC_H_
#define TIKU_RTC_H_

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Initialise the RTC layer. Idempotent.
 *
 * Validates the persist-cell gate and zeroes the baseline when the gate is
 * not valid (first boot, or virgin NVM).
 *
 * @note Call once during boot, before tiku_rtc_get_seconds() or
 *       tiku_rtc_set_seconds().
 */
void tiku_rtc_init(void);

/**
 * @brief Return current wall-clock seconds since the Unix epoch
 *        (or whatever epoch the caller last set).
 *
 * Equivalent to `epoch_base + (uptime - uptime_base)`.  Returns 0 while the
 * gate is invalid or the baseline is 0, as before the first set.
 */
uint32_t tiku_rtc_get_seconds(void);

/**
 * @brief Set the wall clock to `epoch_seconds`.
 *
 * Stores the epoch baseline in durable memory and pairs it with the current
 * boot's uptime.  The persist-cell commit opens the MPU window around the
 * write.
 */
void tiku_rtc_set_seconds(uint32_t epoch_seconds);

/**
 * @brief Set the wall clock, reporting whether the baseline was saved.
 * @return 0 when the persist-cell commit completed, -1 on failure
 */
int tiku_rtc_set_seconds_status(uint32_t epoch_seconds);

/**
 * @brief True when the gate is valid and the baseline is non-zero, as after
 *        any set to a non-zero time.
 */
int tiku_rtc_is_set(void);

#if defined(TIKU_RTC_TEST_HOOKS) && TIKU_RTC_TEST_HOOKS
/**
 * @brief Test-only hook: capture the wall clock and its persist gate.
 *
 * @p epoch receives the reconstructed clock and @p gate the raw gate word;
 * either may be NULL.  Writes nothing to NVM.
 *
 * @param epoch  Out: current wall-clock seconds (NULL to skip)
 * @param gate   Out: raw persist-cell gate word (NULL to skip)
 */
void tiku_rtc_test_snapshot(uint32_t *epoch, uint32_t *gate);

/**
 * @brief Test-only hook: put back a snapshotted clock/gate pair.
 *
 * Writes both into the cell in one unlock window, then re-pairs the baseline
 * with the current uptime.  Restoring a gate other than the magic re-creates
 * the never-set state, so a later init re-primes.
 *
 * @param epoch  Epoch baseline to store
 * @param gate   Gate word to store (the cell magic marks it valid)
 */
void tiku_rtc_test_restore(uint32_t epoch, uint32_t gate);
#endif

#ifdef __cplusplus
}
#endif

#endif /* TIKU_RTC_H_ */
