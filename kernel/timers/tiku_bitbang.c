/*
 * Tiku Operating System v0.06
 * Simple. Ubiquitous. Intelligence, Everywhere.
 * http://tiku-os.org
 *
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 *
 * tiku_bitbang.c - hardware-driven precision bit-bang engine.
 *
 * Two backends behind one API: every port but RP2350 drives each bit edge
 * from an htimer compare-match ISR, and RP2350 shifts bits from a PIO state
 * machine with no per-bit CPU work.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

/*---------------------------------------------------------------------------*/
/* INCLUDES                                                                  */
/*---------------------------------------------------------------------------*/

#include <tiku.h>
#include "tiku_bitbang.h"
#include <interfaces/gpio/tiku_gpio.h>
#include <stddef.h>

/*
 * The software backend toggles the pin from an htimer ISR through the generic
 * tiku_gpio and tiku_htimer APIs.  Every port but RP2350 uses it; RP2350 uses
 * its PIO backend.
 */
#if !defined(TIKU_BITBANG_SOFT) && !defined(PLATFORM_RP2350)
#define TIKU_BITBANG_SOFT 1
#endif

#if defined(TIKU_BITBANG_SOFT)
#include "tiku_htimer.h"
#elif defined(PLATFORM_RP2350)
#include <arch/arm-rp2350/tiku_pio_arch.h>
#endif

/*---------------------------------------------------------------------------*/
/* SHARED MODULE STATE                                                       */
/*---------------------------------------------------------------------------*/

static struct {
    tiku_bitbang_t   cfg;
    volatile uint8_t busy;
#if defined(TIKU_BITBANG_SOFT)
    tiku_htimer_clock_t next_edge;
    uint16_t            bit_idx;
#endif
} bb;

static uint16_t bb_tx_count;

#if defined(TIKU_BITBANG_SOFT)
static struct tiku_htimer bb_htimer;
#endif

/*===========================================================================*/
/* SOFTWARE BACKEND -- htimer ISR per bit                                    */
/*===========================================================================*/

#if defined(TIKU_BITBANG_SOFT)

/** @brief Bit @p bit_idx of the stream, in the configured bit order. */
static inline uint8_t bb_get_bit(uint16_t bit_idx) {
    uint8_t byte = bb.cfg.data[bit_idx >> 3];
    uint8_t pos  = bb.cfg.msb_first ? (uint8_t)(7 - (bit_idx & 7))
                                    : (uint8_t)(bit_idx & 7);
    return (uint8_t)((byte >> pos) & 1);
}

/** @brief htimer callback: drive the next bit and reschedule, or finish. */
static void bb_isr(struct tiku_htimer *t, void *ptr) {
    (void)ptr;

    if (bb.bit_idx < bb.cfg.bit_count) {
        uint8_t v = bb_get_bit(bb.bit_idx);
        tiku_gpio_write(bb.cfg.port, bb.cfg.pin, v);

        bb.bit_idx++;
        bb.next_edge = (tiku_htimer_clock_t)
                       (bb.next_edge + bb.cfg.bit_time_ticks);

        /* No-guard reschedule: bit periods may be shorter than the
         * standard htimer guard time. */
        tiku_htimer_set_no_guard(t, bb.next_edge, bb_isr, NULL);
        return;
    }

    /* Trailing edge: drive idle level, complete. */
    tiku_gpio_write(bb.cfg.port, bb.cfg.pin, bb.cfg.idle_level);
    bb.busy = 0;
    bb_tx_count++;

    if (bb.cfg.on_done != NULL) {
        bb.cfg.on_done(bb.cfg.ctx);
    }
}

/**
 * @brief Drive the pin to its idle level and schedule the first edge one bit
 *        period out, or twice the htimer guard time if that is later.
 * @return TIKU_BITBANG_OK or a negative error code
 */
static int bb_soft_tx(const tiku_bitbang_t *cfg) {
    int rc;
    tiku_htimer_clock_t now, lead;

    bb.bit_idx = 0;

    if (tiku_gpio_dir_out(cfg->port, cfg->pin) != TIKU_GPIO_OK) {
        return TIKU_BITBANG_ERR_INVALID;
    }
    tiku_gpio_write(cfg->port, cfg->pin, cfg->idle_level);

    /* The first edge is set from thread context through the guard check,
     * which reads the counter again: a lead of two guard times leaves one
     * for the counter to move before that read.  The ISR sets the later
     * edges a period apart. */
    lead = cfg->bit_time_ticks;
    if (lead < 2u * TIKU_HTIMER_GUARD_TIME) {
        lead = (tiku_htimer_clock_t)(2u * TIKU_HTIMER_GUARD_TIME);
    }
    now = TIKU_HTIMER_NOW();
    bb.next_edge = (tiku_htimer_clock_t)(now + lead);

    rc = tiku_htimer_set(&bb_htimer, bb.next_edge, bb_isr, NULL);
    if (rc != TIKU_HTIMER_OK) {
        return (rc == TIKU_HTIMER_ERR_TIME)
                 ? TIKU_BITBANG_ERR_TIMING
                 : TIKU_BITBANG_ERR_INVALID;
    }
    return TIKU_BITBANG_OK;
}

/** @brief Cancel the htimer and drive the pin to its idle level. */
static int bb_soft_abort(void) {
    tiku_htimer_cancel();
    tiku_gpio_write(bb.cfg.port, bb.cfg.pin, bb.cfg.idle_level);
    return TIKU_BITBANG_OK;
}

#endif /* TIKU_BITBANG_SOFT */

/*===========================================================================*/
/* RP2350 BACKEND -- PIO state machine                                       */
/*===========================================================================*/

#if defined(PLATFORM_RP2350) && !defined(TIKU_BITBANG_SOFT)

/**
 * @brief Pack data[] into a word in the order the PIO program shifts.
 *
 * MSB-first sends byte[0]'s MSB first, so byte[0] sits in the top byte of the
 * word; LSB-first sends its LSB first, so byte[0] sits in the bottom byte.
 */
static uint32_t bb_pack(const uint8_t *data, uint8_t bit_count,
                        uint8_t msb_first) {
    uint32_t word = 0U;
    uint8_t  byte_count = (uint8_t)((bit_count + 7U) / 8U);
    uint8_t  i;

    if (msb_first) {
        for (i = 0; i < byte_count; i++) {
            word |= ((uint32_t)data[i]) << (24U - (uint32_t)i * 8U);
        }
        /* PIO arch shifts top-of-word first; trailing unused bits in
         * the bottom of the word never make it onto the wire because
         * X is loaded with bit_count - 1. */
    } else {
        for (i = 0; i < byte_count; i++) {
            word |= ((uint32_t)data[i]) << ((uint32_t)i * 8U);
        }
    }
    return word;
}

/**
 * @brief PIO IRQ completion: drive the idle level, count the stream and call
 *        on_done, in ISR context.
 */
static void bb_pio_done(void *ctx) {
    (void)ctx;
    tiku_gpio_write(bb.cfg.port, bb.cfg.pin, bb.cfg.idle_level);
    bb.busy = 0;
    bb_tx_count++;
    if (bb.cfg.on_done != NULL) {
        bb.cfg.on_done(bb.cfg.ctx);
    }
}

/**
 * @brief Hand the packed stream (1..32 bits) to the PIO state machine.
 * @return TIKU_BITBANG_OK or a negative error code
 */
static int bb_rp2350_tx(const tiku_bitbang_t *cfg) {
    uint32_t data_word;
    int rc;

    if (cfg->bit_count == 0U || cfg->bit_count > 32U) {
        return TIKU_BITBANG_ERR_INVALID;
    }
    /* The PIO arch takes the period in microseconds, one RP2350 htimer
     * tick each.  A zero period is refused. */
    if (cfg->bit_time_ticks == 0U) {
        return TIKU_BITBANG_ERR_TIMING;
    }

    /* One-time init -- idempotent. */
    tiku_pio_arch_init();

    data_word = bb_pack(cfg->data, (uint8_t)cfg->bit_count,
                         cfg->msb_first);

    rc = tiku_pio_arch_bitbang_tx(cfg->pin,
                                  data_word,
                                  (uint8_t)cfg->bit_count,
                                  cfg->msb_first,
                                  cfg->bit_time_ticks,
                                  bb_pio_done, NULL);
    if (rc == TIKU_PIO_OK) {
        return TIKU_BITBANG_OK;
    }
    if (rc == TIKU_PIO_ERR_BUSY) {
        return TIKU_BITBANG_ERR_BUSY;
    }
    return TIKU_BITBANG_ERR_INVALID;
}

/** @brief Stop the PIO stream and drive the pin to its idle level. */
static int bb_rp2350_abort(void) {
    (void)tiku_pio_arch_bitbang_abort();
    tiku_gpio_write(bb.cfg.port, bb.cfg.pin, bb.cfg.idle_level);
    return TIKU_BITBANG_OK;
}

#endif /* PLATFORM_RP2350 */

/*===========================================================================*/
/* PUBLIC API -- platform-agnostic                                           */
/*===========================================================================*/

int tiku_bitbang_tx(const tiku_bitbang_t *cfg) {
    int rc;

    if (cfg == NULL ||
        (cfg->data == NULL && cfg->bit_count > 0) ||
        cfg->bit_time_ticks == 0) {
        return TIKU_BITBANG_ERR_INVALID;
    }
    if (bb.busy) {
        return TIKU_BITBANG_ERR_BUSY;
    }

    bb.cfg  = *cfg;
    bb.busy = 1;

#if defined(TIKU_BITBANG_SOFT)
    rc = bb_soft_tx(cfg);
#elif defined(PLATFORM_RP2350)
    rc = bb_rp2350_tx(cfg);
#else
    rc = TIKU_BITBANG_ERR_INVALID;
#endif

    if (rc != TIKU_BITBANG_OK) {
        bb.busy = 0;
    }
    return rc;
}

int tiku_bitbang_busy(void) {
    return bb.busy != 0;
}

int tiku_bitbang_abort(void) {
    if (!bb.busy) {
        return TIKU_BITBANG_ERR_NOT_BUSY;
    }
    bb.busy = 0;
#if defined(TIKU_BITBANG_SOFT)
    return bb_soft_abort();
#elif defined(PLATFORM_RP2350)
    return bb_rp2350_abort();
#else
    return TIKU_BITBANG_OK;
#endif
}

uint16_t tiku_bitbang_tx_count(void) {
    return bb_tx_count;
}
