/*
 * Tiku Operating System v0.05
 * Simple. Ubiquitous. Intelligence, Everywhere.
 * http://tiku-os.org
 *
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 *
 * tiku_rtc.c - Wall-clock RTC implementation
 *
 * Soft real-time clock backing the /sys/time VFS node.  There is no
 * dedicated RTC peripheral here: wall-clock time is reconstructed
 * from the free-running system clock (tiku_clock_seconds(), uptime
 * since this boot) plus a persisted epoch baseline.
 *
 * State model:
 *   - `rtc_epoch_base` lives in `.persistent`; `rtc_uptime_base` is reset-local.
 *     The wall clock equals epoch_base + (uptime - uptime_base).
 *   - Its persist-cell gate (`rtc_cell`) distinguishes a never-set
 *     RTC (return 0) from a real persisted epoch.
 *
 * Boot semantics:
 *   - tiku_rtc_init() checks the magic. On first boot it zeroes the
 *     baseline and writes the magic; on subsequent boots it leaves the
 *     persisted baseline alone.
 *   - On warm reset the persisted epoch becomes the new boot baseline.  This
 *     preserves the last explicitly set value but, without an always-on RTC,
 *     cannot recover time elapsed while reset or unpowered.
 *
 * Persistence and the magic-gate idiom:
 *   The baseline is declared as a persist cell (TIKU_PERSIST_CELL,
 *   kernel/memory): value storage in `.persistent` (FRAM on MSP430,
 *   a flash-mirrored region on RP2350) plus a magic-word gate that
 *   proves the cell holds real data rather than power-on garbage.
 *   The shared cell API owns the MPU unlock window and the
 *   data-before-gate commit ordering this file used to hand-roll;
 *   writes go through tiku_persist_cell_commit() so a set on a
 *   never-initialised device self-validates.  The read paths touch
 *   no lock, so reading the clock never disturbs the MPU.
 *
 * Caveats:
 *   - Uptime restarts at 0 after reset.  The clock therefore resumes from the
 *     last explicitly persisted epoch; it cannot recover time elapsed since
 *     that set, during reset, or while unpowered.  Pairing this with an
 *     external time source (NTP, GNSS, host sync) closes the gap; we expose a
 *     clean set_seconds entry point for that.
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

/**
 * Gate key for the epoch-baseline cell.
 *
 * The bytes spell 'WALD' (wall-clock durable baseline) in ASCII — an arbitrary
 * non-trivial sentinel; the odds of an uninitialised FRAM word
 * happening to match are 1 in 2^32.  When the cell's gate does not
 * hold this value the baseline is treated as virgin: reads return 0
 * and tiku_rtc_init() re-primes the baseline to zero.  Bump the value
 * if the cell's meaning ever changes incompatibly so a stale image
 * re-primes cleanly on the next boot after reflashing.
 */
#define TIKU_RTC_MAGIC  0x57414C44UL /* 'WALD': epoch-baseline layout */

/**
 * FRAM cell: wall-clock epoch paired with this boot's uptime baseline.
 *
 * Lives in `.persistent` so the explicitly set epoch survives reset and power
 * loss.  Read paths add only the uptime elapsed since the baseline was paired
 * on this boot.  Only meaningful while the cell gate validates.
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
 * Delegates to tiku_persist_cell_init(): when the gate already
 * validates the persisted baseline is real and is left untouched —
 * this is the warm/cold reboot path where the wall clock is meant
 * to survive.  Otherwise this is a first boot (or a wiped FRAM):
 * the cell API zeroes the baseline and stamps the gate last, inside
 * its own MPU unlock window.  Called once during boot (from the
 * /sys VFS init path) before any get/set.
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
 * Reconstructs time as epoch_base + (uptime - uptime_base), where uptime
 * comes from tiku_clock_seconds().  Returns 0 if the RTC has
 * never been set since first power-on (magic mismatch) — callers
 * treat a small value as "clock not set".
 *
 * Read-only and lock-free: it inspects FRAM cells but performs no
 * write, so the MPU is never unlocked here.  This is the hot path
 * behind every read of /sys/time.
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
 * Rebases the persisted epoch so that future reads agree with the just-set
 * time: it stores epoch_seconds and pairs that value with the current boot's
 * uptime.  A subsequent get adds only uptime elapsed since that pairing.  The
 * write goes through tiku_persist_cell_commit(), which stores the
 * value and then (re)stamps the gate in one MPU window — so the
 * value becomes valid even if this is the very first set on a
 * virgin FRAM.
 *
 * Snapshots uptime *before* the cell write so the unlock window
 * stays minimal.  This is the write path behind /sys/time and the
 * external-time-sync entry point.
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
 * True only when the magic gate is valid AND the epoch baseline is non-zero.
 * The non-zero test draws a deliberate line between "initialised to
 * defaults" (tiku_rtc_init() stamps the magic but leaves the baseline
 * at 0) and "explicitly set" via tiku_rtc_set_seconds().  Lock-free;
 * no NVM write.
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
