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

/** @brief Non-maskable interrupts seen; a CPU1 lockup raises one. */
extern volatile uint32_t tiku_ra8p1_cpu1_nmi_count;

#endif /* TIKU_RA8P1_CPU1_ARCH_H_ */
