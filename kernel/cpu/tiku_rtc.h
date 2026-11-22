/*
 * Tiku Operating System v0.06
 * Simple. Persistent. Intelligent.
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
 * not valid (first boot, or virgin NVM).  Either way this boot's uptime
 * becomes the pairing baseline; a later call, or one after a set, is a no-op.
 *
 * @note Call once during boot, before tiku_rtc_get_seconds() or
 *       tiku_rtc_set_seconds().
 */
void tiku_rtc_init(void);

/**
 * @brief Return current wall-clock seconds since the Unix epoch
 *        (or whatever epoch the caller last set).
 *
 * Equivalent to `epoch_base + (uptime - uptime_base)`.  Opens no MPU window;
 * callable from any context, a worker thread included.
 *
 * @return Wall-clock seconds; 0 while the gate is invalid or the baseline is
 *         0, as before the first set
 */
uint32_t tiku_rtc_get_seconds(void);

/**
 * @brief Set the wall clock to `epoch_seconds`.
 *
 * Stores the epoch baseline in durable memory and pairs it with the current
 * boot's uptime.  The persist-cell commit opens the MPU window around the
 * write.  It is tiku_rtc_set_seconds_status() with the status discarded.
 */
void tiku_rtc_set_seconds(uint32_t epoch_seconds);

/**
 * @brief Set the wall clock, reporting whether the baseline was saved.
 *
 * Pairs the stored epoch with the current uptime, so later reads add only
 * the time elapsed since.  The commit stores the value, then the gate, so a
 * set also validates a cell that tiku_rtc_init() never primed.
 *
 * @param epoch_seconds  Wall-clock time, seconds since the epoch
 * @return 0 when the persist-cell commit completed, -1 on failure
 */
int tiku_rtc_set_seconds_status(uint32_t epoch_seconds);

/**
 * @brief True when the gate is valid and the baseline is non-zero, as after
 *        any set to a non-zero time.
 *
 * @return Non-zero after a set to a non-zero time, 0 otherwise
 */
int tiku_rtc_is_set(void);

/** @brief What the wall clock's time is worth. */
typedef enum {
    TIKU_RTC_UNSET = 0,  /**< never set, or set to 0: the clock reads 0 */
    TIKU_RTC_SET,        /**< set during this boot: the time is current */
    TIKU_RTC_RESUMED     /**< set before this boot: the stored epoch plus
                          *   this boot's uptime, behind the present by the
                          *   time between that set and this boot */
} tiku_rtc_state_t;

/**
 * @brief Whether the wall clock is unset, set during this boot, or resumed
 *        from a set made before it.
 *
 * tiku_rtc_is_set() is true for both of the last two.  A reader that shows
 * the time as current needs TIKU_RTC_SET.  Reads only; callable from any
 * context, a worker thread included.
 */
tiku_rtc_state_t tiku_rtc_state(void);

/**
 * @brief The wall clock and its state, read together.
 *
 * Reads only, inside one atomic section, so a set made by the kernel
 * thread is seen whole or not at all; callable from any context, a worker
 * thread included.
 *
 * @param seconds Out: what tiku_rtc_get_seconds() returns, 0 when the
 *                state is TIKU_RTC_UNSET (NULL to skip)
 * @return The state those seconds belong to
 */
tiku_rtc_state_t tiku_rtc_read(uint32_t *seconds);

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
 * Writes both into the cell in one unlock window and re-pairs the baseline
 * with the current uptime.  A gate other than the magic re-creates the
 * never-set state; a restored clock counts as resumed, not set this boot.
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
