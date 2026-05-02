/*
 * Tiku Operating System v0.04
 * Simple. Ubiquitous. Intelligence, Everywhere.
 * http://tiku-os.org
 *
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 *
 * tiku_bitbang.c - Hardware-timer-driven bit-bang engine
 *
 * The transmission state machine has two phases:
 *
 *   PHASE_DATA  -- bit_idx < cfg.bit_count
 *                  ISR drives the pin to the next data bit and
 *                  reschedules itself for the bit period later.
 *
 *   PHASE_IDLE  -- bit_idx == cfg.bit_count
 *                  ISR drives the pin to cfg.idle_level, clears the
 *                  busy flag, increments the completion counter,
 *                  and invokes on_done.
 *
 * Drift-free scheduling: each rescheduled edge is computed from the
 * previous scheduled time (next_edge += bit_time_ticks), not from
 * "now", so ISR jitter accumulates in the duty cycle of one bit but
 * not across the stream.
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
#include "tiku_bitbang.h"
#include "tiku_htimer.h"
#include <interfaces/gpio/tiku_gpio.h>
#include <stddef.h>

/*---------------------------------------------------------------------------*/
/* MODULE STATE                                                              */
/*---------------------------------------------------------------------------*/

static struct tiku_htimer bb_htimer;

static struct {
    tiku_bitbang_t      cfg;
    tiku_htimer_clock_t next_edge;
    uint16_t            bit_idx;
    volatile uint8_t    busy;
} bb;

static uint16_t bb_tx_count;

/*---------------------------------------------------------------------------*/
/* INTERNAL                                                                  */
/*---------------------------------------------------------------------------*/

/**
 * Read the @p bit_idx-th bit of the data array, honouring msb_first.
 */
static inline uint8_t
bb_get_bit(uint16_t bit_idx)
{
    uint8_t byte = bb.cfg.data[bit_idx >> 3];
    uint8_t pos  = bb.cfg.msb_first ? (uint8_t)(7 - (bit_idx & 7))
                                    : (uint8_t)(bit_idx & 7);
    return (uint8_t)((byte >> pos) & 1);
}

/**
 * htimer ISR callback. Runs once per bit edge.
 */
static void
bb_isr(struct tiku_htimer *t, void *ptr)
{
    (void)ptr;

    if (bb.bit_idx < bb.cfg.bit_count) {
        /* Drive the next data bit. */
        uint8_t v = bb_get_bit(bb.bit_idx);
        tiku_gpio_write(bb.cfg.port, bb.cfg.pin, v);

        bb.bit_idx++;
        bb.next_edge = (tiku_htimer_clock_t)
                       (bb.next_edge + bb.cfg.bit_time_ticks);

        /* No-guard reschedule: bit periods may be shorter than the
         * standard htimer guard time. The caller is responsible for
         * staying ahead of the hardware counter; if we miss the
         * compare we will silently drop until wraparound. */
        tiku_htimer_set_no_guard(t, bb.next_edge, bb_isr, NULL);
        return;
    }

    /* PHASE_IDLE: trailing edge, return to idle, complete. */
    tiku_gpio_write(bb.cfg.port, bb.cfg.pin, bb.cfg.idle_level);
    bb.busy = 0;
    bb_tx_count++;

    if (bb.cfg.on_done != NULL) {
        bb.cfg.on_done(bb.cfg.ctx);
    }
}

/*---------------------------------------------------------------------------*/
/* PUBLIC API                                                                */
/*---------------------------------------------------------------------------*/

int tiku_bitbang_tx(const tiku_bitbang_t *cfg)
{
    int rc;
    tiku_htimer_clock_t now;

    if (cfg == NULL ||
        (cfg->data == NULL && cfg->bit_count > 0) ||
        cfg->bit_time_ticks == 0) {
        return TIKU_BITBANG_ERR_INVALID;
    }

    if (bb.busy) {
        return TIKU_BITBANG_ERR_BUSY;
    }

    bb.cfg     = *cfg;
    bb.bit_idx = 0;
    bb.busy    = 1;

    /* Initialise the pin: direction = output, level = idle. */
    if (tiku_gpio_dir_out(cfg->port, cfg->pin) != TIKU_GPIO_OK) {
        bb.busy = 0;
        return TIKU_BITBANG_ERR_INVALID;
    }
    tiku_gpio_write(cfg->port, cfg->pin, cfg->idle_level);

    /* Arm the first edge through the standard guarded set so it has
     * to be at least TIKU_HTIMER_GUARD_TIME away. The bit-bang
     * effectively starts one bit period after this call. */
    now = TIKU_HTIMER_NOW();
    bb.next_edge = (tiku_htimer_clock_t)(now + cfg->bit_time_ticks);

    rc = tiku_htimer_set(&bb_htimer, bb.next_edge, bb_isr, NULL);
    if (rc != TIKU_HTIMER_OK) {
        bb.busy = 0;
        if (rc == TIKU_HTIMER_ERR_TIME) {
            return TIKU_BITBANG_ERR_TIMING;
        }
        return TIKU_BITBANG_ERR_INVALID;
    }

    return TIKU_BITBANG_OK;
}

/*---------------------------------------------------------------------------*/

int tiku_bitbang_busy(void)
{
    return bb.busy != 0;
}

/*---------------------------------------------------------------------------*/

int tiku_bitbang_abort(void)
{
    if (!bb.busy) {
        return TIKU_BITBANG_ERR_NOT_BUSY;
    }

    tiku_htimer_cancel();
    bb.busy = 0;
    tiku_gpio_write(bb.cfg.port, bb.cfg.pin, bb.cfg.idle_level);
    return TIKU_BITBANG_OK;
}

/*---------------------------------------------------------------------------*/

uint16_t tiku_bitbang_tx_count(void)
{
    return bb_tx_count;
}
