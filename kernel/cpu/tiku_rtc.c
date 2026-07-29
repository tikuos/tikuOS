/*
 * Tiku Operating System v0.06
 * Simple. Ubiquitous. Intelligence, Everywhere.
 * http://tiku-os.org
 *
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 *
 * tiku_rtc.c - wall-clock RTC implementation.
 *
 * No RTC peripheral: wall clock is uptime plus a persisted epoch baseline held in
 * a persist cell, whose magic gate separates a never-set clock from a real one.
 * Uptime restarts at reset, so time elapsed while unpowered cannot be recovered.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

/*---------------------------------------------------------------------------*/
/* INCLUDES                                                                  */
/*---------------------------------------------------------------------------*/

#include "tiku_rtc.h"
#include <kernel/timers/tiku_clock.h>
#include <kernel/memory/tiku_mem.h>

/*---------------------------------------------------------------------------*/
/* PERSISTENT STATE                                                          */
/*---------------------------------------------------------------------------*/

/*
 * Gate key for the epoch-baseline cell.  An arbitrary non-trivial sentinel: a
 * gate that does not hold it means the baseline is virgin, so reads return 0
 * and init re-primes.  Bump it if the cell's meaning ever changes.
 */
#define TIKU_RTC_MAGIC  0x57414C44UL /* 'WALD': epoch-baseline layout */

/*
 * The wall-clock epoch, paired with this boot's uptime baseline.  Lives in
 * .persistent so an explicitly set epoch survives reset and power loss; reads
 * add only the uptime since the pairing, and only while the gate validates.
 */
static TIKU_DURABLE uint32_t rtc_epoch_base;

/** Uptime paired with rtc_epoch_base in this boot only. */
static uint32_t rtc_uptime_base;
static uint8_t rtc_boot_initialized;

/** Gate + descriptor: defaults to 0 (clock never set) */
TIKU_PERSIST_CELL(rtc_cell, rtc_epoch_base, TIKU_RTC_MAGIC, NULL, 0);

/*---------------------------------------------------------------------------*/
/* PUBLIC FUNCTIONS                                                          */
/*---------------------------------------------------------------------------*/

/**
 * @brief Initialise the soft RTC. Idempotent.
 *
 * A validating gate means the persisted baseline is real and is left alone --
 * the reboot path where the clock is meant to survive.  Otherwise the cell API
 * zeroes the baseline and stamps the gate last, in its own unlock window.
 */
void
tiku_rtc_init(void)
{
    if (!rtc_boot_initialized) {
        (void)tiku_persist_cell_init(&rtc_cell);
        rtc_uptime_base = (uint32_t)tiku_clock_seconds();
        rtc_boot_initialized = 1U;
    }
}

/**
 * @brief Return current wall-clock seconds since the epoch.
 *
 * Reconstructs epoch_base plus the uptime elapsed since the baseline was
 * paired, returning 0 when the clock was never set.  Read-only and lock-free,
 * so the MPU is never unlocked here; this is the hot path behind /sys/time.
 *
 * @return Wall-clock seconds, or 0 if the RTC was never set.
 */
uint32_t
tiku_rtc_get_seconds(void)
{
    uint32_t now;
    if (!tiku_persist_cell_valid(&rtc_cell) || rtc_epoch_base == 0U) {
        return 0;
    }
    now = (uint32_t)tiku_clock_seconds();
    return rtc_epoch_base + (now - rtc_uptime_base);
}

/**
 * @brief Set the wall clock to @p epoch_seconds.
 *
 * Stores the epoch and pairs it with the current uptime, so later reads add
 * only what has elapsed since.  The commit stamps value then gate in one
 * window, so the value is valid even on a virgin store.
 *
 * @param epoch_seconds  Desired wall-clock time, seconds since epoch.
 */
void
tiku_rtc_set_seconds(uint32_t epoch_seconds)
{
    uint32_t now = (uint32_t)tiku_clock_seconds();

    tiku_persist_cell_commit(&rtc_cell, &epoch_seconds,
                             (uint16_t)sizeof(epoch_seconds));
    rtc_uptime_base = now;
    rtc_boot_initialized = 1U;
}

/**
 * @brief Report whether the wall clock holds a real, set value.
 *
 * True only when the gate validates AND the baseline is non-zero -- the line
 * between initialised-to-defaults, which init leaves at 0, and explicitly set.
 *
 * @return Non-zero if the clock has been set at least once since the
 *         chip was first programmed, 0 otherwise.
 */
int
tiku_rtc_is_set(void)
{
    return tiku_persist_cell_valid(&rtc_cell) && rtc_epoch_base != 0;
}

#if defined(TIKU_RTC_TEST_HOOKS) && TIKU_RTC_TEST_HOOKS
void
tiku_rtc_test_snapshot(uint32_t *epoch, uint32_t *gate)
{
    if (epoch != 0) *epoch = tiku_rtc_get_seconds();
    if (gate != 0) *gate = rtc_cell_gate;
}

void
tiku_rtc_test_restore(uint32_t epoch, uint32_t gate)
{
    uint16_t saved = tiku_mpu_unlock_nvm();
    rtc_epoch_base = epoch;
    rtc_cell_gate = gate;
    tiku_mpu_lock_nvm(saved);
    rtc_uptime_base = (uint32_t)tiku_clock_seconds();
    rtc_boot_initialized = gate == TIKU_RTC_MAGIC ? 1U : 0U;
}
#endif
