/*
 * Tiku Operating System v0.06
 * Simple. Persistent. Intelligent.
 * http://tiku-os.org
 *
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 *
 * tiku_rtc.c - wall-clock RTC implementation.
 *
 * No RTC peripheral: the wall clock is uptime plus an epoch baseline held in a
 * persist cell, whose magic gate separates a never-set clock from a real one.
 * A reset loses the time since the last set: the clock resumes from that value.
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
 * Gate key for the epoch-baseline cell.  A gate that does not hold it marks
 * the baseline invalid: reads return 0 and init re-primes.  Change the key
 * when the cell's meaning changes.
 */
#define TIKU_RTC_MAGIC  0x57414C44UL /* 'WALD': epoch-baseline layout */

/*
 * The wall-clock epoch, paired with this boot's uptime baseline.  Durable, so
 * an explicitly set epoch survives reset and power loss; reads add only the
 * uptime since the pairing, and only while the gate validates.
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

void
tiku_rtc_init(void)
{
    if (!rtc_boot_initialized) {
        (void)tiku_persist_cell_init(&rtc_cell);
        rtc_uptime_base = (uint32_t)tiku_clock_seconds();
        rtc_boot_initialized = 1U;
    }
}

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

int
tiku_rtc_set_seconds_status(uint32_t epoch_seconds)
{
    uint32_t now = (uint32_t)tiku_clock_seconds();

    tiku_mem_err_t status = tiku_persist_cell_commit_status(
        &rtc_cell, &epoch_seconds, (uint16_t)sizeof(epoch_seconds));
    rtc_uptime_base = now;
    rtc_boot_initialized = 1U;
    return status == TIKU_MEM_OK ? 0 : -1;
}

void tiku_rtc_set_seconds(uint32_t epoch_seconds)
{
    (void)tiku_rtc_set_seconds_status(epoch_seconds);
}

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
