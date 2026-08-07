/*
 * Tiku Operating System v0.06
 * Simple. Ubiquitous. Intelligence, Everywhere.
 * http://tiku-os.org
 *
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 *
 * tiku_cpu1_ipc.h - the page the M85 and the Cortex-M33 share.
 *
 * Compiled by both toolchains, so it is the single statement of the layout
 * and the protocol; neither side carries its own copy of an offset.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#ifndef TIKU_RA8P1_CPU1_IPC_H_
#define TIKU_RA8P1_CPU1_IPC_H_

#include <stdint.h>

/*
 * Image geometry.  The loader copies the image to a 128-byte-aligned buffer
 * of TIKU_CPU1_AREA_SIZE and the payload derives its own base from the PC,
 * so these are offsets, not addresses.
 */
#define TIKU_CPU1_AREA_SIZE     2048U
#define TIKU_CPU1_RESET_OFF     0x40U    /* entry, patched into vector 1   */
#define TIKU_CPU1_SHARED_OFF    0x200U   /* the struct below               */
#define TIKU_CPU1_STACK_OFF     TIKU_CPU1_AREA_SIZE

/** @brief Bytes a single message carries in either direction. */
#define TIKU_CPU1_MSG_CAP       48U

/** @brief What the payload publishes once it is executing C. */
#define TIKU_CPU1_MAGIC         0x4D333350UL    /* 'P33M' in memory order */

/** @brief The M85's D-cache line, and so the granule of every maintenance op. */
#define TIKU_CPU1_LINE          32U

/*
 * THE PAGE IS SPLIT BY WRITER, NOT BY MEANING.
 *
 * The M85 cleans the lines it owns and invalidates the lines CPU1 owns.  A
 * line written by both would lose one side's data on whichever operation ran
 * second -- an invalidate discards a dirty halt request, a clean writes back
 * a stale heartbeat.  So each half is padded to a whole number of lines and
 * nothing crosses.
 *
 * CPU1 runs with its caches off, which is why only the M85 maintains.
 */
typedef struct {
    /* --- written by the M85, read by CPU1 --------------------------- */
    volatile uint32_t halt;         /**< non-zero parks the payload       */
    volatile uint32_t a2c_seq;      /**< bumped last, after buf and len   */
    volatile uint32_t a2c_len;      /**< bytes valid in a2c_buf           */
    volatile uint32_t a2c_rsvd;
    volatile uint8_t  a2c_buf[TIKU_CPU1_MSG_CAP];

    /* --- written by CPU1, read by the M85 --------------------------- */
    volatile uint32_t magic;        /**< TIKU_CPU1_MAGIC once running     */
    volatile uint32_t heartbeat;    /**< advances while un-parked         */
    volatile uint32_t c2a_seq;      /**< set to the a2c_seq being answered */
    volatile uint32_t c2a_len;      /**< bytes valid in c2a_buf           */
    volatile uint8_t  c2a_buf[TIKU_CPU1_MSG_CAP];
} tiku_cpu1_shared_t;

/** @brief Byte offset of the half CPU1 writes, for the M85's invalidates. */
#define TIKU_CPU1_C2A_OFF   ((uint32_t)__builtin_offsetof(tiku_cpu1_shared_t, \
                                                          magic))

_Static_assert(sizeof(tiku_cpu1_shared_t) % TIKU_CPU1_LINE == 0,
               "cpu1: the shared page must be a whole number of cache lines");
_Static_assert(__builtin_offsetof(tiku_cpu1_shared_t, magic) %
               TIKU_CPU1_LINE == 0,
               "cpu1: the two halves must not share a cache line");
_Static_assert(TIKU_CPU1_SHARED_OFF % TIKU_CPU1_LINE == 0,
               "cpu1: the shared page must start on a cache line");
_Static_assert(TIKU_CPU1_SHARED_OFF + sizeof(tiku_cpu1_shared_t) <=
               TIKU_CPU1_AREA_SIZE,
               "cpu1: the shared page runs past the image area");

#endif /* TIKU_RA8P1_CPU1_IPC_H_ */
