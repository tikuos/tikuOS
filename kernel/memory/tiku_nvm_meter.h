/*
 * Tiku Operating System v0.06
 * Simple. Persistent. Intelligent.
 * http://tiku-os.org
 *
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 *
 * tiku_nvm_meter.h - what the kernel's NVM writes have cost since boot.
 *
 * Each port's write primitives report the bytes they stored, the units the
 * medium programmed and erased for them, and the time they waited on the
 * controller.  /sys/mem/nvm_writes prints the totals.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#ifndef TIKU_NVM_METER_H_
#define TIKU_NVM_METER_H_

#include <stdint.h>

/**
 * @brief Totals since boot over the metered write paths.
 *
 * A program is one program of the medium's unit (tiku_nvm_media.h): a
 * wordline on RRAM, a 16- or 32-byte line on MRAM, a page on flash.
 */
typedef struct {
    uint32_t bytes;      /**< bytes the metered operations stored          */
    uint32_t programs;   /**< units the medium programmed for them         */
    uint32_t erases;     /**< erase units erased                           */
    uint32_t busy_us;    /**< microseconds spent in the metered operations */
} tiku_nvm_spent_t;

/** @brief A start mark taken by tiku_nvm_meter_begin(). */
typedef uint32_t tiku_nvm_mark_t;

/**
 * @brief Mark the start of one program or erase operation.
 *
 * The first call starts the bench timebase (tiku_bench.h).
 */
tiku_nvm_mark_t tiku_nvm_meter_begin(void);

/**
 * @brief Charge the operation begun at @p start: its bytes, programs and
 *        erases, and the time since @p start.
 *
 * On a 16-bit timebase one operation must end within the counter's period.
 */
void tiku_nvm_meter_end(tiku_nvm_mark_t start, uint32_t bytes,
                        uint32_t programs, uint32_t erases);

/**
 * @brief Charge one aligned store of @p bytes into durable memory written in
 *        place: the program units it covers, untimed.
 */
void tiku_nvm_meter_store(uint32_t bytes);

/**
 * @brief Units of @p unit bytes (a power of two) that [@p addr, @p addr +
 *        @p len) touches; 0 when @p len is 0.
 */
uint32_t tiku_nvm_units(uintptr_t addr, uint32_t len, uint32_t unit);

/** @brief Copy the totals since boot into @p out. */
void tiku_nvm_spent(tiku_nvm_spent_t *out);

#endif /* TIKU_NVM_METER_H_ */
