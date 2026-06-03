/*
 * Tiku Operating System v0.05
 * Simple. Ubiquitous. Intelligence, Everywhere.
 * http://tiku-os.org
 *
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 *
 * tiku_bitbang.c - Hardware-driven precision bit-bang engine
 *
 * Two backends share a single kernel API surface (tiku_bitbang_tx,
 * busy, abort, tx_count):
 *
 *   MSP430  -- Timer A1 (htimer) ISR per bit edge.  Each transition
 *              is driven from software, but the bit clock is the
 *              hardware compare-match so jitter stays sub-microsecond
 *              and the CPU is free between edges.
 *
 *   RP2350  -- PIO0 state machine.  A 4-instruction PIO program
 *              shifts bits from the TX FIFO to the configured GPIO at
 *              SM clock rate; the CPU loads the data word, configures
 *              the divider, and waits for a PIO IRQ.  No per-bit CPU
 *              work at all, and the bit rate can reach clk_sys/2 -- a
 *              hundred-megahertz ceiling instead of a few-hundred-
 *              kilohertz htimer-ISR ceiling.
 *
 * Kernel-side state (busy flag, completion counter, the cfg snapshot
 * for the idle-level write and the user callback) is shared.  The
 * platform-specific scheduling code is inside #if blocks below.
 *
 * Limits on the RP2350 PIO backend:
 *   - bit_count must be in [1, 32] per call.  Longer bursts will
 *     need either multiple tx calls or extending the PIO program to
 *     pull more than one word; not implemented yet.
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

#if defined(PLATFORM_MSP430)
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
#if defined(PLATFORM_MSP430)
    tiku_htimer_clock_t next_edge;
    uint16_t            bit_idx;
#endif
} bb;

static uint16_t bb_tx_count;

#if defined(PLATFORM_MSP430)
static struct tiku_htimer bb_htimer;
#endif

/*===========================================================================*/
/* MSP430 BACKEND -- htimer ISR per bit                                       */
/*===========================================================================*/

#if defined(PLATFORM_MSP430)

static inline uint8_t bb_get_bit(uint16_t bit_idx) {
    uint8_t byte = bb.cfg.data[bit_idx >> 3];
    uint8_t pos  = bb.cfg.msb_first ? (uint8_t)(7 - (bit_idx & 7))
                                    : (uint8_t)(bit_idx & 7);
    return (uint8_t)((byte >> pos) & 1);
}

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

static int bb_msp430_tx(const tiku_bitbang_t *cfg) {
    int rc;
    tiku_htimer_clock_t now;

    bb.bit_idx = 0;

    if (tiku_gpio_dir_out(cfg->port, cfg->pin) != TIKU_GPIO_OK) {
        return TIKU_BITBANG_ERR_INVALID;
    }
    tiku_gpio_write(cfg->port, cfg->pin, cfg->idle_level);

    now = TIKU_HTIMER_NOW();
    bb.next_edge = (tiku_htimer_clock_t)(now + cfg->bit_time_ticks);

    rc = tiku_htimer_set(&bb_htimer, bb.next_edge, bb_isr, NULL);
    if (rc != TIKU_HTIMER_OK) {
        return (rc == TIKU_HTIMER_ERR_TIME)
                 ? TIKU_BITBANG_ERR_TIMING
                 : TIKU_BITBANG_ERR_INVALID;
    }
    return TIKU_BITBANG_OK;
}

static int bb_msp430_abort(void) {
    tiku_htimer_cancel();
    tiku_gpio_write(bb.cfg.port, bb.cfg.pin, bb.cfg.idle_level);
    return TIKU_BITBANG_OK;
}

#endif /* PLATFORM_MSP430 */

/*===========================================================================*/
/* RP2350 BACKEND -- PIO state machine                                        */
/*===========================================================================*/

#if defined(PLATFORM_RP2350)

/* Pack data[] into a uint32_t in the order the PIO program shifts.
 * For MSB-first the first byte's MSB goes first, so byte[0] sits in
 * the top byte of the word.  For LSB-first the first byte's LSB goes
 * first, so byte[0] sits in the bottom byte. */
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

/* PIO IRQ handler thunk -- runs in ISR context, finishes the
 * transaction with the idle-level write + user callback. */
static void bb_pio_done(void *ctx) {
    (void)ctx;
    tiku_gpio_write(bb.cfg.port, bb.cfg.pin, bb.cfg.idle_level);
    bb.busy = 0;
    bb_tx_count++;
    if (bb.cfg.on_done != NULL) {
        bb.cfg.on_done(bb.cfg.ctx);
    }
}

static int bb_rp2350_tx(const tiku_bitbang_t *cfg) {
    uint32_t data_word;
    int rc;

    if (cfg->bit_count == 0U || cfg->bit_count > 32U) {
        return TIKU_BITBANG_ERR_INVALID;
    }
    /* bit_time_ticks is microseconds in the htimer "high accuracy"
     * preset shared with htimer; the PIO arch wants microseconds too,
     * but caps at uint16_t.  Reject pathologically long periods. */
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

static int bb_rp2350_abort(void) {
    (void)tiku_pio_arch_bitbang_abort();
    tiku_gpio_write(bb.cfg.port, bb.cfg.pin, bb.cfg.idle_level);
    return TIKU_BITBANG_OK;
}

#endif /* PLATFORM_RP2350 */

/*===========================================================================*/
/* PUBLIC API -- platform-agnostic                                            */
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

#if defined(PLATFORM_MSP430)
    rc = bb_msp430_tx(cfg);
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
#if defined(PLATFORM_MSP430)
    return bb_msp430_abort();
#elif defined(PLATFORM_RP2350)
    return bb_rp2350_abort();
#else
    return TIKU_BITBANG_OK;
#endif
}

uint16_t tiku_bitbang_tx_count(void) {
    return bb_tx_count;
}
