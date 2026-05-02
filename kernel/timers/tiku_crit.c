/*
 * Tiku Operating System v0.04
 * Simple. Ubiquitous. Intelligence, Everywhere.
 * http://tiku-os.org
 *
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 *
 * tiku_crit.c - Critical execution window implementation
 *
 * Two flavours: defer-only (no IE masking) and masked
 * (selective IE masking via preserve_mask). End restores whatever
 * begin set up; the mode is recorded so end can short-circuit the
 * unmask path when nothing was masked in the first place.
 *
 * The masked flavour delegates the actual IE-bit save/clear/restore
 * to the platform via hal/tiku_crit_hal.h. Everything in this file
 * is platform-agnostic: held flag, mode, accounting, htimer-based
 * duration measurement, and the post-window timer-process drain.
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at:
 *
 *     http://www.apache.org/licenses/LICENSE-2.0
 *
 * SPDX-License-Identifier: Apache-2.0
 */

/*---------------------------------------------------------------------------*/
/* INCLUDES                                                                  */
/*---------------------------------------------------------------------------*/

#include <tiku.h>
#include "tiku_crit.h"
#include "tiku_htimer.h"
#include "tiku_timer.h"
#include <hal/tiku_crit_hal.h>

/*---------------------------------------------------------------------------*/
/* MODES                                                                     */
/*---------------------------------------------------------------------------*/

#define CRIT_MODE_NONE   0  /* Window not held */
#define CRIT_MODE_DEFER  1  /* Defer-only; no IE bits touched */
#define CRIT_MODE_MASKED 2  /* IE bits masked per preserve_mask */

/*---------------------------------------------------------------------------*/
/* MODULE STATE                                                              */
/*---------------------------------------------------------------------------*/

volatile uint8_t tiku_crit_held;

static tiku_htimer_clock_t crit_start_htime;
static uint16_t            crit_max_us;
static uint16_t            crit_violations;
static uint16_t            crit_enters;
static uint8_t             crit_mode;

/*---------------------------------------------------------------------------*/
/* INTERNAL HELPERS                                                          */
/*---------------------------------------------------------------------------*/

/**
 * Convert microseconds to htimer ticks at compile-time-known rate.
 *
 * Two-step division avoids 32-bit overflow at 1 MHz htimer × 65 535 us
 * (which would be 6.5e10, larger than 2^32). Requires htimer >= 1 kHz,
 * which is true for every preset in tiku_htimer_config.h.
 */
static inline tiku_htimer_clock_t
crit_us_to_ticks(uint16_t us)
{
#if (TIKU_HTIMER_SECOND >= 1000UL)
    return (tiku_htimer_clock_t)
           (((uint32_t)us * (TIKU_HTIMER_SECOND / 1000UL)) / 1000UL);
#else
    return (tiku_htimer_clock_t)
           (((uint32_t)us * TIKU_HTIMER_SECOND) / 1000000UL);
#endif
}

/*---------------------------------------------------------------------------*/
/* PUBLIC API                                                                */
/*---------------------------------------------------------------------------*/

int tiku_crit_begin(uint16_t max_us, uint8_t preserve_mask)
{
    if (tiku_crit_held) {
        return TIKU_CRIT_ERR_BUSY;
    }

    crit_start_htime = TIKU_HTIMER_NOW();
    crit_max_us      = max_us;
    crit_enters++;

    /*
     * Mask first, then set the held flag. The reverse order would
     * leave a few cycles where tiku_crit_active() returns true
     * but the not-yet-masked ISRs can still fire and inject jitter.
     */
    tiku_crit_arch_mask_irqs(preserve_mask);
    crit_mode      = CRIT_MODE_MASKED;
    tiku_crit_held = 1;
    return TIKU_CRIT_OK;
}

/*---------------------------------------------------------------------------*/

int tiku_crit_begin_defer(uint16_t max_us)
{
    if (tiku_crit_held) {
        return TIKU_CRIT_ERR_BUSY;
    }

    crit_start_htime = TIKU_HTIMER_NOW();
    crit_max_us      = max_us;
    crit_enters++;

    /* No IE-bit changes; just flip the dispatcher-defer flag. */
    crit_mode      = CRIT_MODE_DEFER;
    tiku_crit_held = 1;
    return TIKU_CRIT_OK;
}

/*---------------------------------------------------------------------------*/

int tiku_crit_end(void)
{
    tiku_htimer_clock_t elapsed_ticks;
    uint8_t was_mode;

    if (!tiku_crit_held) {
        return TIKU_CRIT_ERR_NOT_HELD;
    }

    elapsed_ticks = (tiku_htimer_clock_t)
                    (TIKU_HTIMER_NOW() - crit_start_htime);

    /* Snapshot and clear mode + held flag before restoring IRQs
     * so any tick that fires immediately on unmask follows the
     * normal poll path. */
    was_mode       = crit_mode;
    crit_mode      = CRIT_MODE_NONE;
    tiku_crit_held = 0;

    if (was_mode == CRIT_MODE_MASKED) {
        tiku_crit_arch_unmask_irqs();
    }

    if (crit_max_us != 0) {
        tiku_htimer_clock_t budget_ticks = crit_us_to_ticks(crit_max_us);
        if (elapsed_ticks > budget_ticks) {
            crit_violations++;
        }
    }

    /* Drain any timer expirations that came due during the window. */
    tiku_timer_request_poll();

    return TIKU_CRIT_OK;
}

/*---------------------------------------------------------------------------*/

uint16_t tiku_crit_violation_count(void)
{
    return crit_violations;
}

uint16_t tiku_crit_enter_count(void)
{
    return crit_enters;
}
