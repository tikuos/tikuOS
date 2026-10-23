/*
 * Tiku Operating System v0.06
 * Simple. Persistent. Intelligent.
 * http://tiku-os.org
 *
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 *
 * tiku_camera_arch.h - P4 camera: a Sony IMX219 (Raspberry Pi Camera v2) on
 * I2C0 streaming RAW10 over two CSI-2 lanes, a frame through the ISP into
 * memory as RGB565, and the frame's luma statistics.
 *
 * SPDX-License-Identifier: Apache-2.0
 */
#ifndef TIKU_ESP32P4_CAMERA_ARCH_H_
#define TIKU_ESP32P4_CAMERA_ARCH_H_
#include <stdint.h>
#include "tiku_csi_arch.h"
#include "tiku_isp_arch.h"

#define TIKU_P4_CAM_OK            0
#define TIKU_P4_CAM_ERR_PARAM    (-1)   /**< null, mode out of range, value
                                         *   out of the register's range */
#define TIKU_P4_CAM_ERR_ABSENT   (-2)   /**< no IMX219 answers at 0x10 */
#define TIKU_P4_CAM_ERR_BUS      (-3)   /**< a register access failed */
#define TIKU_P4_CAM_ERR_LINK     (-4)   /**< the receiver did not start */
#define TIKU_P4_CAM_ERR_TIMEOUT  (-5)   /**< no frame within the wait */
#define TIKU_P4_CAM_ERR_FAULT    (-6)   /**< the frame ended with an ISP,
                                         *   bridge or DMA error */
#define TIKU_P4_CAM_ERR_STATE    (-7)   /**< no mode configured yet */

#define TIKU_P4_CAM_I2C_ADDR      0x10u
#define TIKU_P4_CAM_MODEL         0x0219u
#define TIKU_P4_CAM_LANES         2u
#define TIKU_P4_CAM_LANE_MBPS     912u  /**< the 456 MHz link, both lanes */
#define TIKU_P4_CAM_MODES         2u
#define TIKU_P4_CAM_EXPOSURE_MIN  4u    /**< lines */
#define TIKU_P4_CAM_GAIN_MAX      232u  /**< analogue code: 256 / (256 - code) */
#define TIKU_P4_CAM_WAIT_US       500000u /**< the frame after stream-on */

/** @brief A mode: the frame's size, its frame length in lines. */
struct tiku_p4_cam_mode {
    uint16_t width, height;
    uint16_t vts;
};

/** @brief What one capture measured. */
struct tiku_p4_cam_frame {
    uint32_t us;                        /**< stream-on to the frame's end */
    uint32_t stop_before;               /**< the lanes' stop states before
                                         *   stream-on (TIKU_P4_CSI_STOP_*) */
    struct tiku_p4_isp_stats isp;       /**< AE means and error bits */
    struct tiku_p4_csi_status csi;      /**< the receiver's errors */
};

/** @brief Processing a capture adds: white-balance gains on red and blue,
 *         and a 1/2.2 gamma curve. */
struct tiku_p4_cam_tune {
    uint16_t gain_r, gain_b;    /**< 2.10 fixed point, 1024 = 1.0, below
                                 *   4096; both 0 leave the colour as is */
    uint8_t gamma;              /**< 1: the gamma curve on */
};

/** @brief Luma of an RGB565 frame: mean, extremes, 16 equal bins. */
struct tiku_p4_cam_luma {
    uint8_t mean, min, max;
    uint32_t hist[16];
    uint32_t pixels;
};

/** @brief Mode @p mode's description, or NULL past the last. */
const struct tiku_p4_cam_mode *tiku_p4_cam_mode(unsigned mode);
/** @brief Read the sensor's model register into @p model.
 *  @return TIKU_P4_CAM_OK, ERR_ABSENT or ERR_BUS. */
int tiku_p4_cam_identify(uint16_t *model);
/** @brief Reset the sensor and load mode @p mode with the default exposure
 *         and gain; the sensor stays in standby. */
int tiku_p4_cam_configure(unsigned mode);
/** @brief Exposure in lines, TIKU_P4_CAM_EXPOSURE_MIN .. VTS - 4. */
int tiku_p4_cam_set_exposure(uint32_t lines);
/** @brief Analogue gain code, 0 .. TIKU_P4_CAM_GAIN_MAX. */
int tiku_p4_cam_set_gain(uint32_t code);
/** @brief The configured mode, or -1 before the first configure. */
int tiku_p4_cam_current_mode(void);
/** @brief The exposure in lines and the gain code now set. */
void tiku_p4_cam_exposure_now(uint32_t *lines, uint32_t *code);
/**
 * @brief Capture one frame of the configured mode as RGB565 into @p rgb565.
 *
 * Starts the receiver, arms the ISP, streams the sensor until the frame
 * ends or TIKU_P4_CAM_WAIT_US passes and puts it back in standby; the ISP
 * measures the AE block means and the whole frame's AWB sums.
 *
 * @param rgb565 width x height x 2 bytes, 8-byte aligned, in HP SRAM or PSRAM
 * @param tune   white balance and gamma to apply, or NULL for neither
 * @param f      filled with the timing and the error status, all zero
 *               after a refusal, or NULL
 * @return TIKU_P4_CAM_OK, or ERR_STATE, ERR_PARAM, ERR_LINK, ERR_BUS,
 *         ERR_TIMEOUT or ERR_FAULT
 */
int tiku_p4_cam_capture(void *rgb565, const struct tiku_p4_cam_tune *tune,
                        struct tiku_p4_cam_frame *f);
/** @brief Grey-world white-balance gains from @p f's AWB sums into @p t
 *         (2.10 fixed point, clamped to 0.5 .. 3.99); 1.0 each without a
 *         white pixel. */
void tiku_p4_cam_balance(const struct tiku_p4_cam_frame *f,
                         struct tiku_p4_cam_tune *t);
/** @brief The mean of @p f's 25 AE block means. */
uint32_t tiku_p4_cam_ae_mean(const struct tiku_p4_cam_frame *f);
/**
 * @brief Steer exposure, then gain, until a frame's AE mean is within a
 *        fifth of @p target (at most @p rounds frames), then capture a final
 *        frame with grey-world white balance and the gamma curve.
 *
 * @param rgb565 as tiku_p4_cam_capture()
 * @param tune   receives the gains applied to the final frame, all zero
 *               when a frame fails before it
 * @param f      the last frame's measurements
 * @param used   receives the frames captured, the final one included
 * @return as tiku_p4_cam_capture()
 */
int tiku_p4_cam_auto(void *rgb565, uint32_t target, uint32_t rounds,
                     struct tiku_p4_cam_tune *tune, struct tiku_p4_cam_frame *f,
                     uint32_t *used);
/** @brief The luma of every @p step-th pixel of every @p step-th row of a
 *         @p width x @p height RGB565 frame into @p out. */
void tiku_p4_cam_luma(const void *rgb565, uint32_t width, uint32_t height,
                      uint32_t step, struct tiku_p4_cam_luma *out);
#endif
