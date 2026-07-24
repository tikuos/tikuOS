/*
 * Tiku Operating System v0.06
 * Simple. Ubiquitous. Intelligence, Everywhere.
 * http://tiku-os.org
 *
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 *
 * tiku_nvm_map.c - NVM region table and backing storage
 *
 * Declares durable arrays sized by device-header constants.  The
 * linker places them — no hardcoded addresses.  A static region table
 * provides runtime lookup by ID.
 *
 * The backing is TIKU_DURABLE (.persistent) on EVERY platform — FRAM
 * in place on MSP430, RRAM in place on nRF54L, SRAM+NVM-mirror on
 * RP2350/Ambiq.  (Until the 2026-07 audit this was MSP430-only, which
 * silently made the CONFIG region — the inittab! — volatile on every
 * Cortex-M part: `init add` entries vanished at each reboot.  Audit
 * finding P0-1; see kintsugi/memory.md §10c.)
 *
 * SPDX-License-Identifier: Apache-2.0
 */

/*---------------------------------------------------------------------------*/
/* INCLUDES                                                                  */
/*---------------------------------------------------------------------------*/

#include "tiku_nvm_map.h"
#include "tiku.h"
#include "tiku_mem.h"    /* TIKU_DURABLE, tiku_region_claim */

/*---------------------------------------------------------------------------*/
/* BACKING STORAGE                                                           */
/*---------------------------------------------------------------------------*/

/** Config region — used by init table, future credentials, etc.
 *  No initializer: it lands in a NOLOAD durable section on the mirror
 *  parts, and every consumer (tiku_init's magic word) primes virgin
 *  content itself — same discipline as the persist cells. */
static TIKU_DURABLE uint8_t
    nvm_config_buf[TIKU_DEVICE_FRAM_CONFIG_SIZE];

/*
 * Future: app slot arrays go here, guarded by TIKU_NVM_APP_ENABLE.
 *
 * #if TIKU_NVM_APP_ENABLE
 * static NVM_PERSISTENT uint8_t
 *     nvm_app0[TIKU_DEVICE_NVM_APP_SLOT_SIZE] = {0};
 * ...
 * #endif
 */

/*---------------------------------------------------------------------------*/
/* REGION TABLE                                                              */
/*---------------------------------------------------------------------------*/

static const tiku_nvm_region_t regions[] = {
    {
        .base  = nvm_config_buf,
        .size  = TIKU_DEVICE_FRAM_CONFIG_SIZE,
        .id    = TIKU_NVM_REGION_CONFIG,
        .flags = TIKU_NVM_REGION_ACTIVE
    },
    /* Future app slots will be added here */
};

#define REGION_COUNT (sizeof(regions) / sizeof(regions[0]))

/*---------------------------------------------------------------------------*/
/* PUBLIC FUNCTIONS                                                          */
/*---------------------------------------------------------------------------*/

/**
 * @brief Initialise the NVM region map.
 *
 * Call once during early boot, before any subsystem that uses
 * tiku_nvm_region_get().  Registers each active region as a claim in
 * the kernel region registry so overlap with any other subsystem's
 * range is a validated error rather than silent aliasing.  Claim
 * failure is non-fatal (the registry may be full, or the region
 * table on a host build may not classify the section); lookups keep
 * working either way.
 */
void
tiku_nvm_map_init(void)
{
    uint8_t i;

    for (i = 0; i < REGION_COUNT; i++) {
        if (regions[i].flags & TIKU_NVM_REGION_ACTIVE) {
            (void)tiku_region_claim(regions[i].base,
                                    (tiku_mem_arch_size_t)regions[i].size,
                                    (uint8_t)regions[i].id);
        }
    }
}

/**
 * @brief Look up an NVM region by its stable identifier.
 *
 * Scans the static region table for an active entry matching @p id.
 * Returns a read-only pointer to the region descriptor, which
 * includes the base address and size.
 *
 * @param id  Region identifier (e.g. TIKU_NVM_REGION_CONFIG).
 * @return    Pointer to the region descriptor, or NULL if @p id is
 *            not found or the region is inactive.
 *
 * @see tiku_nvm_region_count()
 */
const tiku_nvm_region_t *
tiku_nvm_region_get(tiku_nvm_region_id_t id)
{
    uint8_t i;

    for (i = 0; i < REGION_COUNT; i++) {
        if (regions[i].id == id &&
            (regions[i].flags & TIKU_NVM_REGION_ACTIVE)) {
            return &regions[i];
        }
    }
    return (const tiku_nvm_region_t *)0;
}

/**
 * @brief Return the number of active NVM regions.
 *
 * Counts only regions whose TIKU_NVM_REGION_ACTIVE flag is set.
 * Inactive or future-reserved slots are excluded.
 *
 * @return Number of active regions.
 *
 * @see tiku_nvm_region_get()
 */
uint8_t
tiku_nvm_region_count(void)
{
    uint8_t i;
    uint8_t count = 0;

    for (i = 0; i < REGION_COUNT; i++) {
        if (regions[i].flags & TIKU_NVM_REGION_ACTIVE) {
            count++;
        }
    }
    return count;
}
