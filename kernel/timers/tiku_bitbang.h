/*
 * Tiku Operating System v0.04
 * Simple. Ubiquitous. Intelligence, Everywhere.
 * http://tiku-os.org
 *
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 *
 * tiku_bitbang.h - Hardware-timer-driven precision bit-bang engine
 *
 * Drives a single GPIO pin through an arbitrary bit pattern at a
 * caller-specified bit rate. The bit clock is supplied by the htimer
 * subsystem (Timer A1 on MSP430), so each pin transition fires from
 * an ISR with sub-microsecond scheduling resolution. The CPU is free
 * between transitions.
 *
 * Intended for backscatter, software UART, software SPI, IR remote,
 * and similar protocols where edge timing matters but full hardware
 * support (SPI/UART peripherals) is unavailable or already in use.
 *
 * The transmitter is one-shot per call. Only one bit-bang stream may
 * be active at a time -- the htimer is single-pending.
 *
 * Recommended use:
 *   1. Caller wraps tiku_bitbang_tx() in
 *        tiku_crit_begin(max_us, TIKU_CRIT_PRESERVE_HTIMER)
 *        ...
 *        tiku_crit_end()
 *      so every other ISR (system tick, UART, ADC, ...) is masked
 *      and only the bit-clock ISR fires between edges.
 *   2. The on_done callback runs in ISR context. Keep it short; it
 *      typically just sets a flag the foreground task is waiting on.
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at:
 *
 *     http://www.apache.org/licenses/LICENSE-2.0
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

    /** Bit period in htimer ticks. With the default HIGH_ACCURACY
     *  preset 1 tick = 1 us, so bit_time_ticks is the bit period in
     *  microseconds. Must be at least a few ticks to allow the ISR
     *  to reschedule before the next compare-match. See note below
     *  on guard-time bypass. */
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
 * Configures the pin as output, schedules the first bit edge via
 * htimer, and returns immediately. Subsequent edges are produced
 * by the htimer ISR. The caller polls tiku_bitbang_busy() or waits
 * on the completion callback.
 *
 * Bit periods shorter than TIKU_HTIMER_GUARD_TIME are accepted: the
 * engine bypasses the guard via tiku_htimer_set_no_guard() during
 * back-to-back rescheduling. The first edge still goes through the
 * standard guarded set, so very-short periods require enough margin
 * before the first edge.
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
