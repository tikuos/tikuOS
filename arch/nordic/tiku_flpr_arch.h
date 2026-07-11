/*
 * Tiku Operating System v0.05
 * Simple. Ubiquitous. Intelligence, Everywhere.
 * http://tiku-os.org
 *
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 *
 * tiku_flpr_arch.h - nRF54L15 FLPR (VPR RISC-V) coprocessor control.
 *
 * App-core side of the coprocessor: load the embedded FLPR image into the
 * SRAM carve, start/stop the core (VPR00 INITPC + CPURUN), and read the
 * liveness state the firmware publishes through the shared page
 * (arch/nordic/flpr/tiku_flpr_ipc.h).
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#ifndef TIKU_NORDIC_FLPR_ARCH_H_
#define TIKU_NORDIC_FLPR_ARCH_H_

#include <stdint.h>

/**
 * @brief Copy the embedded FLPR image into the carve and start the core.
 *
 * Idempotent: restarting reloads the image (fresh .data/.bss world) and
 * re-arms INITPC before CPURUN.
 *
 * @return 0 on success, negative if the embedded image is missing/oversized.
 */
int tiku_flpr_arch_start(void);

/** @brief Stop the coprocessor (CPURUN=Stopped). Idempotent. */
void tiku_flpr_arch_stop(void);

/** @brief 1 when CPURUN reads Running. */
int tiku_flpr_arch_running(void);

/** @brief 1 when the firmware has stamped its magic (reached main()). */
int tiku_flpr_arch_alive(void);

/** @brief Current heartbeat counter from the shared page. */
uint32_t tiku_flpr_arch_heartbeat(void);

/** @brief Embedded image size in bytes (0 if the build carries none). */
uint32_t tiku_flpr_arch_image_size(void);

#endif /* TIKU_NORDIC_FLPR_ARCH_H_ */
