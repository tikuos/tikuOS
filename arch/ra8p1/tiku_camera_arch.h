/*
 * Tiku Operating System v0.06
 * Simple. Ubiquitous. Intelligence, Everywhere.
 * http://tiku-os.org
 *
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 *
 * tiku_camera_arch.h - RA8P1 camera bring-up: clock, power, sensor access.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#ifndef TIKU_RA8P1_CAMERA_ARCH_H_
#define TIKU_RA8P1_CAMERA_ARCH_H_

#include <stdint.h>

#define TIKU_CAM_OK             0
#define TIKU_CAM_ERR_STATE     -1   /**< clock or power step failed        */
#define TIKU_CAM_ERR_ABSENT    -2   /**< sensor does not answer            */
#define TIKU_CAM_ERR_ID        -3   /**< answers, but not as an OV5640     */
#define TIKU_CAM_ERR_BUS       -4   /**< I2C failure mid-transfer          */

/**
 * @brief Start the sensor's external clock and run its power-up sequence.
 *
 * @note The sensor does nothing at all without XCLK -- not even answer on
 *       the bus -- so this must precede any register access.
 *
 * @return TIKU_CAM_OK, or a negative error
 */
int tiku_camera_arch_power_on(void);

/**
 * @brief Read the sensor's chip identity.
 *
 * @param id  Receives the 16-bit ID; an OV5640 reads 0x5640
 * @return TIKU_CAM_OK, or a negative error
 */
int tiku_camera_arch_read_id(uint16_t *id);

/**
 * @brief Read one sensor register.
 *
 * @param reg  16-bit register address
 * @param val  Receives the byte
 * @return TIKU_CAM_OK, or a negative error
 */
int tiku_camera_arch_read_reg(uint16_t reg, uint8_t *val);

/**
 * @brief Write one sensor register.
 *
 * @param reg  16-bit register address
 * @param val  Byte to write
 * @return TIKU_CAM_OK, or a negative error
 */
int tiku_camera_arch_write_reg(uint16_t reg, uint8_t val);

/** @brief The geometry and lane timing tiku_camera_arch_setup_qvga() sets. */
#define TIKU_CAM_QVGA_W         320U
#define TIKU_CAM_QVGA_H         240U
#define TIKU_CAM_UI_PS          2710U   /* 369 Mbps per lane, two lanes */

/**
 * @brief Configure the sensor: QVGA YCbCr422 over two MIPI lanes.
 *
 * @note Ends with the sensor awake but its MIPI output still gated; start
 *       frames with tiku_camera_arch_stream() once the receiver is ready.
 *
 * @return TIKU_CAM_OK, or a negative error
 */
int tiku_camera_arch_setup_qvga(void);

/**
 * @brief Gate or release the sensor's MIPI output.
 *
 * @param on  Non-zero to stream frames, zero to hold them
 * @return TIKU_CAM_OK, or a negative error
 */
int tiku_camera_arch_stream(int on);

#endif /* TIKU_RA8P1_CAMERA_ARCH_H_ */
