/*
 * Tiku Operating System
 * http://tiku-os.org
 *
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 *
 * tiku_rtc.c - Wall-clock RTC implementation
 *
 * State model:
 *   - `rtc_epoch_offset` lives in `.persistent`. The wall clock at
 *     any instant equals `rtc_epoch_offset + tiku_clock_seconds()`.
 *   - `rtc_magic` is the gate that distinguishes a never-set RTC
 *     (return 0) from a real persisted epoch.
 *
 * Boot semantics:
 *   - tiku_rtc_init() checks the magic. On first boot it zeroes the
 *     offset and writes the magic; on subsequent boots it leaves the
 *     persisted offset alone.
 *   - Because `.persistent` is preserved across warm reset and
 *     power cycle (where NVM-backed), the wall clock survives a
 *     reboot without needing to re-set it.
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

#include "tiku_rtc.h"
#include <kernel/timers/tiku_clock.h>
#include <kernel/memory/tiku_mem.h>

#define TIKU_RTC_MAGIC  0x57414C43UL /* 'WALC' */

static uint32_t __attribute__((section(".persistent")))
    rtc_epoch_offset;
static uint32_t __attribute__((section(".persistent")))
    rtc_magic;

void
tiku_rtc_init(void)
{
    uint16_t saved;

    if (rtc_magic == TIKU_RTC_MAGIC) {
        return;
    }

    saved = tiku_mpu_unlock_nvm();
    rtc_epoch_offset = 0;
    rtc_magic        = TIKU_RTC_MAGIC;
    tiku_mpu_lock_nvm(saved);
}

uint32_t
tiku_rtc_get_seconds(void)
{
    if (rtc_magic != TIKU_RTC_MAGIC) {
        return 0;
    }
    return rtc_epoch_offset + (uint32_t)tiku_clock_seconds();
}

void
tiku_rtc_set_seconds(uint32_t epoch_seconds)
{
    uint32_t now = (uint32_t)tiku_clock_seconds();
    uint16_t saved;

    saved = tiku_mpu_unlock_nvm();
    rtc_epoch_offset = epoch_seconds - now;
    rtc_magic        = TIKU_RTC_MAGIC;
    tiku_mpu_lock_nvm(saved);
}

int
tiku_rtc_is_set(void)
{
    return rtc_magic == TIKU_RTC_MAGIC && rtc_epoch_offset != 0;
}
