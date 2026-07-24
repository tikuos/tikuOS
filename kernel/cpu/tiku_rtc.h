/*
 * Tiku Operating System v0.06
 * Simple. Ubiquitous. Intelligence, Everywhere.
 * http://tiku-os.org
 *
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 *
 * tiku_rtc.h - Wall-clock RTC API
 *
 * Soft RTC implemented on top of tiku_clock_seconds() (system uptime)
 * plus a persistent epoch baseline. Stored in `.persistent`, so the last
 * explicitly set epoch survives warm reset on every platform, and survives
 * power cycle wherever `.persistent` is flash- or FRAM-backed
 * (MSP430 FRAM, RP2350 with flash NVM mirror).
 *
 * Resolution is one second. Sub-second time stays available through
 * the existing tiku_clock_arch_fine() / tiku_htimer_now() APIs.
 *
 * This API backs the /sys/time VFS node; the implementation in
 * tiku_rtc.c keeps the persistent baseline and the MPU-unlock
 * handshake out of the VFS handlers.
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
 * Validates the magic word in `.persistent` storage and zeroes the
 * baseline if it has not been seen before (first boot, or virgin
 * NVM). Must be called once during boot, before any rtc_get / rtc_set.
 */
void tiku_rtc_init(void);

/**
 * @brief Return current wall-clock seconds since the Unix epoch
 *        (or whatever epoch the caller last set).
 *
 * Equivalent to `epoch_base + (uptime - uptime_base)`. Returns 0 if
 * the RTC has never been set since first power-on.
 */
uint32_t tiku_rtc_get_seconds(void);

/**
 * @brief Set the wall clock to `epoch_seconds`.
 *
 * Stores the epoch baseline in persistent NVM and pairs it with the current
 * boot's uptime. A short MPU-unlock window is opened around the write.
 */
void tiku_rtc_set_seconds(uint32_t epoch_seconds);

/**
 * @brief True iff the RTC has been set at least once since the
 *        chip was first programmed.
 */
int tiku_rtc_is_set(void);

#if defined(TIKU_RTC_TEST_HOOKS) && TIKU_RTC_TEST_HOOKS
/**
 * @brief TEST-ONLY hook: capture the wall clock and its persist gate.
 *
 * Exists so the test suite can save the RTC state it is about to
 * perturb and put it back afterwards; it is not part of the normal
 * API and is compiled only when TIKU_RTC_TEST_HOOKS is set.  @p epoch
 * receives the *reconstructed* wall clock (what tiku_rtc_get_seconds()
 * returns right now, so 0 when the clock was never set), @p gate the
 * raw magic word that validates the persisted baseline.  Read-only:
 * no NVM write, no MPU unlock.  Either pointer may be NULL to skip
 * that field.
 *
 * @param epoch  Out: current wall-clock seconds (NULL to skip)
 * @param gate   Out: raw persist-cell gate word (NULL to skip)
 */
void tiku_rtc_test_snapshot(uint32_t *epoch, uint32_t *gate);

/**
 * @brief TEST-ONLY hook: put back a snapshotted clock/gate pair.
 *
 * Counterpart of tiku_rtc_test_snapshot(), for the test suite only;
 * compiled with TIKU_RTC_TEST_HOOKS.  Writes @p epoch and @p gate
 * straight into the persistent cell inside one MPU-unlock window,
 * then re-pairs the baseline with the current uptime so a later
 * tiku_rtc_get_seconds() resumes from @p epoch.  The boot-init flag
 * is set from @p gate, so restoring a gate other than the cell magic
 * deliberately re-creates the never-set state and lets a later
 * tiku_rtc_init() re-prime the cell.
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
