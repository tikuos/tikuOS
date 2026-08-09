/*
 * Tiku Operating System v0.06
 * Simple. Ubiquitous. Intelligence, Everywhere.
 * http://tiku-os.org
 *
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 *
 * tiku_vin_arch.h - RA8P1 MIPI CSI-2 capture into memory.
 *
 * The receive pipeline behind the camera: D-PHY clocked by the sensor, the
 * CSI-2 receiver unpacking its packets, and the VIN converting YCbCr to
 * RGB565 and writing frames to a buffer.  Polled; no interrupts.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#ifndef TIKU_RA8P1_VIN_ARCH_H_
#define TIKU_RA8P1_VIN_ARCH_H_

#include <stdint.h>

#define TIKU_VIN_OK             0
#define TIKU_VIN_ERR_STATE     -1   /**< power or PHY never reported ready  */
#define TIKU_VIN_ERR_INVALID   -2   /**< buffer or geometry refused         */
#define TIKU_VIN_ERR_TIMEOUT   -3   /**< no frame arrived                   */

/**
 * @brief Bring up the PHY, CSI receiver and VIN for one geometry.
 *
 * @note @p fb must be 64-byte aligned and hold w * h RGB565 pixels; the VIN
 *       writes it as a bus master, so the caller owns cache maintenance.
 *       The sensor is expected to stream YCbCr422 on virtual channel 0.
 *
 * @param fb     Frame buffer the VIN writes into
 * @param w      Frame width in pixels, a multiple of 16
 * @param h      Frame height in pixels
 * @param ui_ps  One high-speed lane unit interval, in picoseconds
 * @return TIKU_VIN_OK, or a negative error
 */
int tiku_vin_arch_init(void *fb, uint16_t w, uint16_t h, uint32_t ui_ps);

/**
 * @brief Start continuous capture; frames land in the buffer as they come.
 *
 * @return TIKU_VIN_OK, or TIKU_VIN_ERR_STATE before init
 */
int tiku_vin_arch_start(void);

/**
 * @brief Wait until a whole frame has been written, bounded.
 *
 * @return TIKU_VIN_OK once a frame completed, or TIKU_VIN_ERR_TIMEOUT
 */
int tiku_vin_arch_frame_wait(void);

/** @brief The VIN's raw status word, for diagnostics. */
uint32_t tiku_vin_arch_status(void);

/** @brief The VIN's raw interrupt-status word, for diagnostics. */
uint32_t tiku_vin_arch_ints(void);

#endif /* TIKU_RA8P1_VIN_ARCH_H_ */
