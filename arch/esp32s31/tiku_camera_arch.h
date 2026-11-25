/*
 * Tiku Operating System v0.06
 * Simple. Persistent. Intelligent.
 * http://tiku-os.org
 *
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 *
 * tiku_camera_arch.h - Korvo DVP camera: the SmartSens SC101IOT on the J4
 * socket, 1280x720 UYVY frames received by LCD_CAM through AXI DMA into PSRAM.
 *
 * SPDX-License-Identifier: Apache-2.0
 */
#ifndef TIKU_S31_CAMERA_ARCH_H_
#define TIKU_S31_CAMERA_ARCH_H_
#include <stdint.h>

#define TIKU_S31_CAM_OK            0
#define TIKU_S31_CAM_BUSY          1    /**< a capture is still receiving */
#define TIKU_S31_CAM_ERR_PARAM    (-1)  /**< frame not 64-byte aligned PSRAM,
                                         *   too small, or divider out of range */
#define TIKU_S31_CAM_ERR_ABSENT   (-2)  /**< no SC101IOT answers at 0x68 */
#define TIKU_S31_CAM_ERR_BUS      (-3)  /**< a sensor register access failed */
#define TIKU_S31_CAM_ERR_STATE    (-4)  /**< not kernel context, open twice, not
                                         *   open, a pin taken, no capture armed */
#define TIKU_S31_CAM_ERR_TIMEOUT  (-5)  /**< no whole frame within the wait */
#define TIKU_S31_CAM_ERR_FAULT    (-6)  /**< the DMA reported a descriptor or
                                         *   end-of-frame error */

#define TIKU_S31_CAM_ADDR         0x68u
#define TIKU_S31_CAM_MODEL        0xda4au
#define TIKU_S31_CAM_W            1280u
#define TIKU_S31_CAM_H            720u
#define TIKU_S31_CAM_FRAME_BYTES  (TIKU_S31_CAM_W * TIKU_S31_CAM_H * 2u)
/** Dividers of the 40 MHz crystal for the sensor clock on GPIO55: 4 gives
 *  10 MHz (19 MHz pixel clock, 7.8 frames a second), 5 gives 8 MHz. */
#define TIKU_S31_CAM_XCLK_DIV_MIN 4u
#define TIKU_S31_CAM_XCLK_DIV_MAX 5u
#define TIKU_S31_CAM_WAIT_US      2000000u /**< arming to a whole frame */

/** @brief Cumulative capture counters since open. */
typedef struct {
    uint32_t frames;        /**< whole frames delivered */
    uint32_t short_frames;  /**< frames that ended before FRAME_BYTES; the
                             *   capture started inside them */
    uint32_t long_frames;   /**< frames that filled the buffer without ending */
    uint32_t dma_errors;
    uint32_t frame_us;      /**< arming to the last delivered frame */
    uint16_t model;         /**< the sensor's ID, read at open */
} tiku_s31_camera_stats_t;

/**
 * @brief Start the sensor clock, identify and set up the SC101IOT, and
 *        route its pins to LCD_CAM; kernel context.
 *
 * Requires the I2C bus open (tiku_i2c_init) and the memory tiers ready.
 * Claims GPIO46-57 and a 64-byte aligned PSRAM arena for the descriptors.
 * Every failure releases what open took.
 *
 * @param xclk_div Divider of the 40 MHz crystal, XCLK_DIV_MIN..XCLK_DIV_MAX
 */
int tiku_s31_camera_open(unsigned xclk_div);

/**
 * @brief Arm the receiver for one whole frame into @p frame; kernel context.
 *
 * @p frame is 64-byte aligned in mapped PSRAM and holds FRAME_BYTES; the
 * caller does not touch it until tiku_s31_camera_poll() returns 0 or an error.
 */
int tiku_s31_camera_capture(void *frame, uint32_t bytes);

/**
 * @brief Advance an armed capture and copy the counters to @p stats if set;
 *        kernel context.
 *
 * Drops and re-arms a frame that ends short or overruns the buffer.
 * @return BUSY while receiving; 0 once @p frame holds a whole frame with its
 *         cache lines invalidated; an error, after which no capture is armed
 */
int tiku_s31_camera_poll(tiku_s31_camera_stats_t *stats);

/** @brief Stop the sensor and the receiver, restore the pins and release
 *         the descriptors and the LCD_CAM reference; kernel context. */
int tiku_s31_camera_close(void);
#endif
