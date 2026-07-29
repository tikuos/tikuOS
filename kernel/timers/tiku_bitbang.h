/*
 * Tiku Operating System v0.06
 * Simple. Ubiquitous. Intelligence, Everywhere.
 * http://tiku-os.org
 *
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 *
 * tiku_bitbang.h - hardware-timer-driven precision bit-bang engine.
 *
 * Drives one GPIO pin through an arbitrary bit pattern at a caller-set rate, with
 * the bit clock supplied by hardware so the CPU is free between transitions.
 * One-shot per call, and only one stream may be active at a time.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#ifndef TIKU_BITBANG_H_
#define TIKU_BITBANG_H_

#include <stdint.h>
#include "tiku_htimer.h"

/*---------------------------------------------------------------------------*/
/* RETURN CODES                                                              */
/*---------------------------------------------------------------------------*/

#define TIKU_BITBANG_OK            0
#define TIKU_BITBANG_ERR_BUSY     -1  /**< A transmission is in progress */
#define TIKU_BITBANG_ERR_INVALID  -2  /**< Bad config (NULL data, pin, etc.) */
#define TIKU_BITBANG_ERR_TIMING   -3  /**< bit_time_ticks too small */
#define TIKU_BITBANG_ERR_NOT_BUSY -4  /**< abort with no active transmission */

/*---------------------------------------------------------------------------*/
/* CALLBACK TYPE                                                             */
/*---------------------------------------------------------------------------*/

/**
 * @brief Completion callback. Runs in htimer ISR context.
 * @param ctx The ctx pointer from tiku_bitbang_t
 */
typedef void (*tiku_bitbang_done_cb_t)(void *ctx);

/*---------------------------------------------------------------------------*/
/* CONFIG STRUCT                                                             */
/*---------------------------------------------------------------------------*/

/**
 * @struct tiku_bitbang_t
 * @brief Bit-bang transmission descriptor.
 *
 * Lifetime: the caller must keep @p data alive until on_done fires.
 * The struct itself is copied internally so it does not need to
 * outlive the call to tiku_bitbang_tx().
 */
typedef struct {
    uint8_t  port;            /**< GPIO port (1..N, or 0xFF for port J) */
    uint8_t  pin;             /**< Pin within port (0..7) */
    uint8_t  msb_first;       /**< 1 = MSB of each byte first; 0 = LSB first */
    uint8_t  idle_level;      /**< Pin level driven after the last bit */

    /** Bit period in htimer ticks.  With the default HIGH_ACCURACY preset one
     *  tick is 1 us.  Must be at least a few ticks so the ISR can reschedule
     *  before the next compare-match; see the guard-time note below. */
    uint16_t bit_time_ticks;

    const uint8_t *data;      /**< Bit array, MSB-first or LSB-first per flag */
    uint16_t       bit_count; /**< Number of bits to transmit (>=0) */

    tiku_bitbang_done_cb_t on_done; /**< NULL = no completion callback */
    void                  *ctx;     /**< Opaque pointer passed to on_done */
} tiku_bitbang_t;

/*---------------------------------------------------------------------------*/
/* CORE API                                                                  */
/*---------------------------------------------------------------------------*/

/**
 * @brief Begin transmitting the configured bit stream.
 * @param cfg Caller-owned config; copied internally
 * @return TIKU_BITBANG_OK or a negative error code
 *
 * Configures the pin, schedules the first edge and returns; the ISR produces
 * the rest.  Periods shorter than the htimer guard time are accepted -- the
 * engine bypasses the guard when rescheduling, but the first edge still uses it.
 */
int tiku_bitbang_tx(const tiku_bitbang_t *cfg);

/**
 * @brief Return non-zero while a transmission is in progress.
 *
 * Cleared after the final idle-level edge and before on_done fires.
 */
int tiku_bitbang_busy(void);

/**
 * @brief Abort the in-progress transmission.
 *
 * Cancels the pending htimer and drives the pin to the configured
 * idle level. on_done is NOT called for an aborted stream.
 */
int tiku_bitbang_abort(void);

/**
 * @brief Number of completed transmissions since boot.
 *
 * Counts to-completion only; aborted streams are excluded.
 */
uint16_t tiku_bitbang_tx_count(void);

#endif /* TIKU_BITBANG_H_ */
