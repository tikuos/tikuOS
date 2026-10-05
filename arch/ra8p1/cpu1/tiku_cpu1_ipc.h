/*
 * Tiku Operating System v0.06
 * Simple. Ubiquitous. Intelligence, Everywhere.
 * http://tiku-os.org
 *
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 *
 * tiku_cpu1_ipc.h - the page the M85 and the Cortex-M33 share.
 *
 * Compiled into both the M85 image and the CPU1 payload, so both use one
 * layout and protocol.  tiku_cpu1.ld repeats the offsets and must match.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#ifndef TIKU_RA8P1_CPU1_IPC_H_
#define TIKU_RA8P1_CPU1_IPC_H_

#include <stdint.h>

/*
 * Image geometry.  The payload is linked at TIKU_CPU1_AREA_ADDR, the carve
 * r7ka8p1kf.ld reserves at the bottom of SRAM, and is not relocatable.  The
 * _OFF values are offsets from that base: the reset code finds the base from
 * its PC, and the fault and NMI handlers from VTOR.
 */
#define TIKU_CPU1_AREA_ADDR     0x22000000UL
#define TIKU_CPU1_AREA_SIZE     16384U
#define TIKU_CPU1_RESET_OFF     0x40U    /* entry, patched into vector 1   */
#define TIKU_CPU1_FAULT_OFF     0x1800U  /* HardFault, patched into vec 3  */
#define TIKU_CPU1_NMI_OFF       0x1880U  /* WDT1 NMI, patched into vec 2   */
#define TIKU_CPU1_SHARED_OFF    0x1900U  /* the struct below               */
#define TIKU_CPU1_STACK_OFF     TIKU_CPU1_AREA_SIZE  /* initial SP: area top */

/*
 * Bytes a message carries in either direction.  It holds the largest job, an
 * ECDSA P-256 verify: a 4-byte tag and five 32-byte operands (public point,
 * hash, r, s), 164 bytes.
 */
#define TIKU_CPU1_MSG_CAP       192U

/** @brief What the payload publishes once it is executing C. */
#define TIKU_CPU1_MAGIC         0x4D333350UL    /* 'P33M' in memory order */

/** @brief Magic after a HardFault; the magic word is the whole record. */
#define TIKU_CPU1_MAGIC_FAULT   0x4D333321UL    /* '!33M' in memory order */
#define TIKU_CPU1_MAGIC_HANG    0x4D333323UL    /* '#33M': WDT1 caught a spin */

/** @brief Mailbox message that makes the payload fault itself, for the
 *         bench suite's fault leg.  Four bytes, "FLT!". */
#define TIKU_CPU1_FAULT_MSG     "FLT!"

/*
 * Work message, exactly TIKU_CPU1_MSG_CAP bytes: 'HSH!', iterations (LE u32)
 * and a 40-byte seed.  The reply is the 32-byte chained digest, or the seed
 * as received when iterations is 0.  The heartbeat stops and WDT1 goes
 * unrefreshed while the chain runs; TIKU_CPU1_WORK_MAX_ITERS caps its length,
 * and tiku_ra8p1_cpu1_alive() reports a stopped heartbeat as a dead payload.
 */
#define TIKU_CPU1_WORK_MAGIC0   'H'
#define TIKU_CPU1_WORK_MAGIC1   'S'
#define TIKU_CPU1_WORK_MAGIC2   'H'
#define TIKU_CPU1_WORK_MAGIC3   '!'
#define TIKU_CPU1_WORK_MAX_ITERS 2000000UL

/*
 * Verify job: 'ECV!' then qx, qy, hash, r, s -- five 32-byte big-endian
 * fields, the operands of an X.509 chain link.  The reply is one byte: 1
 * when the signature verifies, 0 when it does not.
 */
#define TIKU_CPU1_VERIFY_MAGIC0 'E'
#define TIKU_CPU1_VERIFY_MAGIC1 'C'
#define TIKU_CPU1_VERIFY_MAGIC2 'V'
#define TIKU_CPU1_VERIFY_MAGIC3 '!'
#define TIKU_CPU1_VERIFY_LEN    (4U + (5U * 32U))

/** @brief The M85's D-cache line size, the granule of its maintenance. */
#define TIKU_CPU1_LINE          32U

/*
 * The page is split by writer.  The M85 cleans the lines it writes and
 * invalidates the lines CPU1 writes; on a line written by both, one of those
 * operations would lose data (an invalidate drops a pending halt request, a
 * clean writes back a stale heartbeat).  Each half is padded to whole lines.
 *
 * CPU1's S-Cache is on, but its MPU marks this page non-cacheable, so only
 * the M85 holds cached copies.
 */
/** @brief The page the M85 and CPU1 share, at TIKU_CPU1_SHARED_OFF. */
typedef struct {
    /* --- written by the M85, read by CPU1 --------------------------- */
    volatile uint32_t halt;         /**< non-zero parks the payload       */
    volatile uint32_t a2c_seq;      /**< bumped last, after buf and len   */
    volatile uint32_t a2c_len;      /**< bytes valid in a2c_buf           */
    volatile uint32_t a2c_restart;  /**< changed = restart a faulted core */
    volatile uint8_t  a2c_buf[TIKU_CPU1_MSG_CAP];
    /* Pads this half to whole cache lines; the asserts below fail when a
     * change to the cap breaks that. */
    volatile uint8_t  a2c_pad[16];

    /* --- written by CPU1, read by the M85 --------------------------- */
    volatile uint32_t magic;        /**< TIKU_CPU1_MAGIC once running     */
    volatile uint32_t heartbeat;    /**< advances while un-parked         */
    volatile uint32_t c2a_seq;      /**< set to the a2c_seq being answered */
    volatile uint32_t c2a_len;      /**< bytes valid in c2a_buf           */
    volatile uint8_t  c2a_buf[TIKU_CPU1_MSG_CAP];
    volatile uint8_t  c2a_pad[16];
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
