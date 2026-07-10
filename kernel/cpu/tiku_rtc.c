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
 * since this boot) plus a single persisted epoch offset.
 *
 * State model:
 *   - `rtc_epoch_offset` lives in `.persistent`. The wall clock at
 *     any instant equals `rtc_epoch_offset + tiku_clock_seconds()`.
 *   - Its persist-cell gate (`rtc_cell`) distinguishes a never-set
 *     RTC (return 0) from a real persisted epoch.
 *
 * Boot semantics:
 *   - tiku_rtc_init() checks the magic. On first boot it zeroes the
 *     offset and writes the magic; on subsequent boots it leaves the
 *     persisted offset alone.
 *   - Because `.persistent` is preserved across warm reset and
 *     power cycle (where NVM-backed), the wall clock survives a
 *     reboot without needing to re-set it.
 *
 * Persistence and the magic-gate idiom:
 *   The offset is declared as a persist cell (TIKU_PERSIST_CELL,
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
 *   - The offset is referenced to *uptime since this boot*, not
 *     absolute monotonic time. After a cold boot, uptime restarts at
 *     0, so on first read after reboot the wall clock jumps to
 *     match the persisted offset (i.e. the clock loses the elapsed
 *     power-off interval, just like a system without a true RTC
 *     crystal). Pairing this with an external time source (NTP,
 *     GNSS, host sync) closes the gap; we expose a clean set_seconds
 *     entry point for that.
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
 * Gate key for the epoch-offset cell.
 *
 * The bytes spell 'WALC' (wall clock) in ASCII — an arbitrary
 * non-trivial sentinel; the odds of an uninitialised FRAM word
 * happening to match are 1 in 2^32.  When the cell's gate does not
 * hold this value the offset is treated as virgin: reads return 0
 * and tiku_rtc_init() re-primes the offset to zero.  Bump the value
 * if the cell's meaning ever changes incompatibly so a stale image
 * re-primes cleanly on the next boot after reflashing.
 */
#define TIKU_RTC_MAGIC  0x57414C43UL /* 'WALC' */

/**
 * FRAM cell: seconds to add to uptime to obtain wall-clock time.
 *
 * Lives in `.persistent` so it survives reset and power loss.  Set
 * by tiku_rtc_set_seconds() to (epoch_seconds - current uptime);
 * read paths add the live uptime back to recover wall-clock seconds.
 * Only meaningful while the cell gate validates.
 */
static uint32_t __attribute__((section(".persistent")))
    rtc_epoch_offset;

/** Gate + descriptor: defaults to 0 (clock never set) */
TIKU_PERSIST_CELL(rtc_cell, rtc_epoch_offset, TIKU_RTC_MAGIC, NULL, 0);

/*---------------------------------------------------------------------------*/
/* PUBLIC FUNCTIONS                                                          */
/*---------------------------------------------------------------------------*/

/**
 * @brief Initialise the soft RTC. Idempotent.
 *
 * Delegates to tiku_persist_cell_init(): when the gate already
 * validates the persisted offset is real and is left untouched —
 * this is the warm/cold reboot path where the wall clock is meant
 * to survive.  Otherwise this is a first boot (or a wiped FRAM):
 * the cell API zeroes the offset and stamps the gate last, inside
 * its own MPU unlock window.  Called once during boot (from the
 * /sys VFS init path) before any get/set.
 */
void
tiku_rtc_init(void)
{
    (void)tiku_persist_cell_init(&rtc_cell);
}

/**
 * @brief Return current wall-clock seconds since the epoch.
 *
 * Reconstructs the time as rtc_epoch_offset + live uptime, where
 * uptime comes from tiku_clock_seconds().  Returns 0 if the RTC has
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
    if (!tiku_persist_cell_valid(&rtc_cell)) {
        return 0;
    }
    return rtc_epoch_offset + (uint32_t)tiku_clock_seconds();
}

/**
 * @brief Set the wall clock to @p epoch_seconds.
 *
 * Rebases the persisted offset so that future reads agree with the
 * just-set time: it stores (epoch_seconds - current uptime), then
 * a subsequent get adds uptime back to recover epoch_seconds.  The
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
    uint32_t off = epoch_seconds - now;

    tiku_persist_cell_commit(&rtc_cell, &off, (uint16_t)sizeof(off));
}

/**
 * @brief Report whether the wall clock holds a real, set value.
 *
 * True only when the magic gate is valid AND the offset is non-zero.
 * The non-zero test draws a deliberate line between "initialised to
 * defaults" (tiku_rtc_init() stamps the magic but leaves the offset
 * at 0) and "explicitly set" via tiku_rtc_set_seconds().  Lock-free;
 * no NVM write.
 *
 * @return Non-zero if the clock has been set at least once since the
 *         chip was first programmed, 0 otherwise.
 */
int
tiku_rtc_is_set(void)
{
    return tiku_persist_cell_valid(&rtc_cell) && rtc_epoch_offset != 0;
}
