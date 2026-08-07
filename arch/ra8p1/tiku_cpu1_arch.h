/*
 * Tiku Operating System v0.06
 * Simple. Ubiquitous. Intelligence, Everywhere.
 * http://tiku-os.org
 *
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 *
 * tiku_cpu1_arch.h - lifecycle of the RA8P1's Cortex-M33.
 *
 * Start, stop and observe a payload on the second core.  Stop is cooperative
 * and there is no path back to power gating; both are hardware facts.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#ifndef TIKU_RA8P1_CPU1_ARCH_H_
#define TIKU_RA8P1_CPU1_ARCH_H_

#include <stdint.h>

/** @brief What the payload writes to its first shared word once executing. */
#define TIKU_RA8P1_CPU1_MAGIC   0x4D333350UL

/** @brief Start outcomes; ignoring these leaves a silently dead core. */
#define TIKU_RA8P1_CPU1_OK          0
#define TIKU_RA8P1_CPU1_ERR_ACT    -1   /**< never left power gating */
#define TIKU_RA8P1_CPU1_ERR_IMG    -2   /**< embedded image absent or too big */
#define TIKU_RA8P1_CPU1_ERR_LEN    -3   /**< message longer than the mailbox */
#define TIKU_RA8P1_CPU1_ERR_DEAD   -4   /**< locked up; a reset revives it */

/**
 * @brief Load the payload and release CPU1, or resume one already running.
 *
 * @return TIKU_RA8P1_CPU1_OK, or TIKU_RA8P1_CPU1_ERR_ACT
 */
int tiku_ra8p1_cpu1_start(void);

/**
 * @brief Ask the payload to halt, and wait until its heartbeat settles.
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
 * @return Non-zero when the payload was started and has not been halted
 */
int tiku_ra8p1_cpu1_running(void);

/**
 * @brief The magic word the payload publishes once it executes.
 *
 * @return TIKU_RA8P1_CPU1_MAGIC when the payload has run
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
 * @return Non-zero when the magic is published and the heartbeat moves
 */
int tiku_ra8p1_cpu1_alive(void);

/**
 * @brief Hand a message to the payload.
 *
 * @param data  Bytes to send
 * @param len   How many, at most TIKU_CPU1_MSG_CAP
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
 * @return Bytes the payload replied, or 0 when it has not answered yet
 */
uint32_t tiku_ra8p1_cpu1_reply(void *out, uint32_t cap);

/**
 * @brief Bytes of embedded payload the launch copies in.
 *
 * @return Size of the image built by arch/ra8p1/cpu1/
 */
uint32_t tiku_ra8p1_cpu1_image_size(void);

/** @brief Non-maskable interrupts seen from armed sources; a CPU1 LOCKUP
 *         raises none, so fault reporting is in-band. */
extern volatile uint32_t tiku_ra8p1_cpu1_nmi_count;

/**
 * @brief Consume a reply doorbell, if one has arrived.
 *
 * @return Non-zero when the payload has signalled since the last call
 */
int tiku_ra8p1_cpu1_bell_take(void);

/** @brief Doorbells taken; 0 with a payload running means polling only. */
extern volatile uint32_t tiku_ra8p1_cpu1_bell_count;

/** @brief Faults the payload has reported; survives a warm reset. */
extern volatile uint32_t tiku_ra8p1_cpu1_fault_count;

/** @brief SRAM truth of the shared page: halt, restart, a2c_seq, magic,
 *         heartbeat -- read fresh past every cached copy. */
void tiku_ra8p1_cpu1_raw(uint32_t out[5]);

#endif /* TIKU_RA8P1_CPU1_ARCH_H_ */
