/*
 * Tiku Operating System
 * http://tiku-os.org
 *
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 *
 * tiku_rtc.h - Wall-clock RTC API
 *
 * Soft RTC implemented on top of tiku_clock_seconds() (system uptime)
 * plus a persistent epoch offset. Stored in `.persistent`, so the
 * wall clock survives warm reset on every platform, and survives
 * power cycle wherever `.persistent` is flash- or FRAM-backed
 * (MSP430 FRAM, RP2350 with flash NVM mirror).
 *
 * Resolution is one second. Sub-second time stays available through
 * the existing tiku_clock_arch_fine() / tiku_htimer_now() APIs.
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
 * offset if it has not been seen before (first boot, or virgin
 * NVM). Must be called once during boot, before any rtc_get / rtc_set.
 */
void tiku_rtc_init(void);

/**
 * @brief Return current wall-clock seconds since the Unix epoch
 *        (or whatever epoch the caller last set).
 *
 * Equivalent to `epoch_offset + tiku_clock_seconds()`. Returns 0 if
 * the RTC has never been set since first power-on.
 */
uint32_t tiku_rtc_get_seconds(void);

/**
 * @brief Set the wall clock to `epoch_seconds`.
 *
 * Stores `epoch_offset = epoch_seconds - tiku_clock_seconds()` in
 * persistent NVM. Subsequent reads return values consistent with
 * the just-set time. A short MPU-unlock window is opened around
 * the persistent write.
 */
void tiku_rtc_set_seconds(uint32_t epoch_seconds);

/**
 * @brief True iff the RTC has been set at least once since the
 *        chip was first programmed.
 */
int tiku_rtc_is_set(void);

#ifdef __cplusplus
}
#endif

#endif /* TIKU_RTC_H_ */
