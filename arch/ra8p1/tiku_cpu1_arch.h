/*
 * Tiku Operating System v0.06
 * Simple. Ubiquitous. Intelligence, Everywhere.
 * http://tiku-os.org
 *
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 *
 * tiku_cpu1_arch.h - lifecycle of the RA8P1's Cortex-M33.
 *
 * Start, stop and observe a payload on the second core and exchange messages
 * with it.  Stop is cooperative, and nothing returns CPU1 to power gating.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#ifndef TIKU_RA8P1_CPU1_ARCH_H_
#define TIKU_RA8P1_CPU1_ARCH_H_

#include <stdint.h>

/** @brief Value the payload writes to its magic word once it runs. */
#define TIKU_RA8P1_CPU1_MAGIC   0x4D333350UL

/** @brief Return codes of tiku_ra8p1_cpu1_start() and
 *         tiku_ra8p1_cpu1_send(). */
#define TIKU_RA8P1_CPU1_OK          0
#define TIKU_RA8P1_CPU1_ERR_ACT    -1   /**< CPU1 not active, or not running */
#define TIKU_RA8P1_CPU1_ERR_IMG    -2   /**< embedded image absent or too big */
#define TIKU_RA8P1_CPU1_ERR_LEN    -3   /**< message empty or over the cap */
#define TIKU_RA8P1_CPU1_ERR_DEAD   -4   /**< locked up; a reset revives it */

/**
 * @brief Load the payload and release CPU1.  On a core already active,
 *        resume a halted payload or restart a faulted one.
 *
 * @return TIKU_RA8P1_CPU1_OK, or TIKU_RA8P1_CPU1_ERR_ACT, _ERR_IMG or
 *         _ERR_DEAD
 */
int tiku_ra8p1_cpu1_start(void);

/**
 * @brief Ask the payload to halt, and wait, bounded, until its heartbeat
 *        stops.
 *
 * @note Cooperative: CPUWAIT is sampled only as the core leaves reset, so a
 *       running core cannot be stalled from outside.
 */
void tiku_ra8p1_cpu1_stop(void);

/**
 * @brief Is CPU1 out of power gating?
 *
 * @note Stays true once started; a halted payload still fetches.
 * @return Non-zero when the activation register reports the core active
 */
int tiku_ra8p1_cpu1_active(void);

/**
 * @brief Is a payload counting, as opposed to halted?
 *
 * @return Non-zero from a successful start until a stop, an NMI, or a
 *         fault that tiku_ra8p1_cpu1_magic() sees
 */
int tiku_ra8p1_cpu1_running(void);

/**
 * @brief Read the payload's magic word.
 *
 * A fault or hang value is counted once in tiku_ra8p1_cpu1_fault_count and
 * clears the running state.
 *
 * @return TIKU_CPU1_MAGIC while running, TIKU_CPU1_MAGIC_FAULT or
 *         TIKU_CPU1_MAGIC_HANG after a fault
 */
uint32_t tiku_ra8p1_cpu1_magic(void);

/**
 * @brief The payload's heartbeat counter.
 *
 * @return A value that advances while the payload runs
 */
uint32_t tiku_ra8p1_cpu1_heartbeat(void);

/**
 * @brief Is the payload both loaded and still executing?
 *
 * @return Non-zero when the payload is running, its magic is
 *         TIKU_CPU1_MAGIC and its heartbeat moves across a short spin
 */
int tiku_ra8p1_cpu1_alive(void);

/**
 * @brief Hand a message to the payload.
 *
 * @param data  Bytes to send
 * @param len   How many, 1..TIKU_CPU1_MSG_CAP
 * @return TIKU_RA8P1_CPU1_OK, ERR_LEN, or ERR_ACT when nothing is running
 */
int tiku_ra8p1_cpu1_send(const void *data, uint32_t len);

/**
 * @brief Sequence the payload has answered, for matching against a send.
 *
 * @return The reply sequence read out of the shared page
 */
uint32_t tiku_ra8p1_cpu1_reply_seq(void);

/**
 * @brief Collect the reply to the most recent send.
 *
 * @param out  Destination, or NULL to ask only for the length
 * @param cap  Bytes available at @p out
 * @return Bytes copied (at most @p cap), the reply length when @p out is
 *         NULL, or 0 when the latest send has no reply
 */
uint32_t tiku_ra8p1_cpu1_reply(void *out, uint32_t cap);

/**
 * @brief Bytes of embedded payload the launch copies in.
 *
 * @return Size of the image built by arch/ra8p1/cpu1/
 */
uint32_t tiku_ra8p1_cpu1_image_size(void);

/** @brief NMIs taken by the M85.  A CPU1 lockup raises none; payload faults
 *         are reported through the shared magic word. */
extern volatile uint32_t tiku_ra8p1_cpu1_nmi_count;

/**
 * @brief Consume a reply doorbell, if one has arrived.
 *
 * @return Non-zero when the payload has signalled since the last call
 */
int tiku_ra8p1_cpu1_bell_take(void);

/** @brief Doorbell interrupts received since boot. */
extern volatile uint32_t tiku_ra8p1_cpu1_bell_count;

/** @brief Faults the payload has reported; kept in retained SRAM so it
 *         survives a warm reset. */
extern volatile uint32_t tiku_ra8p1_cpu1_fault_count;

/**
 * @brief Read halt, a2c_restart, a2c_seq, magic and heartbeat from SRAM,
 *        after invalidating the cached page.
 *
 * @param out  Receives the five words, in that order
 */
void tiku_ra8p1_cpu1_raw(uint32_t out[5]);

#endif /* TIKU_RA8P1_CPU1_ARCH_H_ */
