/*
 * Tiku Operating System v0.06
 * Simple. Persistent. Intelligent.
 * http://tiku-os.org
 *
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 *
 * tiku_isp_arch.h - P4 ISP: a RAW Bayer frame pushed in from memory by the
 * DW-GDMA or passed on by the MIPI CSI-2 receiver, demosaiced and
 * processed, and brought back through the CSI bridge, with the AE and AWB
 * statistics of the frame.
 *
 * SPDX-License-Identifier: Apache-2.0
 */
#ifndef TIKU_ESP32P4_ISP_ARCH_H_
#define TIKU_ESP32P4_ISP_ARCH_H_
#include <stdint.h>

#define TIKU_P4_ISP_OK           0
#define TIKU_P4_ISP_ERR_PARAM   (-1)    /**< null, unaligned, out of range */
#define TIKU_P4_ISP_ERR_BUSY    (-2)    /**< a frame is in flight */
#define TIKU_P4_ISP_ERR_FAULT   (-3)    /**< the ISP, bridge or DMA failed */
#define TIKU_P4_ISP_ERR_TIMEOUT (-4)    /**< the frame did not end in time */

/** @brief Input RAW depths: 8 bits a pixel, 10 and 12 packed the MIPI way. */
#define TIKU_P4_ISP_RAW8        0u
#define TIKU_P4_ISP_RAW10       1u
#define TIKU_P4_ISP_RAW12       2u
/** @brief Bayer orders: the colours of the first row's first two pixels
 *         and the second row's. */
#define TIKU_P4_ISP_BGGR        0u
#define TIKU_P4_ISP_GBRG        1u
#define TIKU_P4_ISP_GRBG        2u
#define TIKU_P4_ISP_RGGB        3u
/** @brief Output formats: RGB565 little-endian words, RGB888 as B, G, R
 *         bytes, YUV422 as U Y V Y, YUV420 planar in the ISP's order. */
#define TIKU_P4_ISP_OUT_RGB565  0u
#define TIKU_P4_ISP_OUT_RGB888  1u
#define TIKU_P4_ISP_OUT_YUV422  2u
#define TIKU_P4_ISP_OUT_YUV420  3u
/** @brief The frame as the receiver sent it, the ISP bypassed: a CSI job
 *         only, up to TIKU_P4_ISP_RAW_MAX a side, the blocks and the
 *         statistics not run, the output tiku_p4_isp_in_bytes() long. */
#define TIKU_P4_ISP_OUT_RAW     4u
/** @brief Frame sources: memory through the DW-GDMA, or the next frame the
 *         MIPI CSI-2 receiver passes on. */
#define TIKU_P4_ISP_SRC_MEMORY  0u
#define TIKU_P4_ISP_SRC_CSI     1u

#define TIKU_P4_ISP_WIDTH_MAX   1920u
#define TIKU_P4_ISP_HEIGHT_MAX  1280u
#define TIKU_P4_ISP_HEIGHT_MIN  8u      /* fewer rows never leave the pipeline */
#define TIKU_P4_ISP_RAW_MAX     4095u   /**< a bypassed frame's width and height */
#define TIKU_P4_ISP_CCM_ONE     1024    /**< 1.0 in the matrix's 2.10 fixed
                                         *   point; magnitudes below 4.0 */

/** @brief A window in pixels: [x0, x1) by [y0, y1). */
struct tiku_p4_isp_window {
    uint16_t x0, y0, x1, y1;
};

/** @brief What the ISP measured on the frame. */
struct tiku_p4_isp_stats {
    uint8_t ae_mean[25];        /**< luminance means of the 5 x 5 AE blocks,
                                 *   row by row */
    uint32_t awb_white;         /**< pixels the AWB counted as white */
    uint32_t awb_sum_r;         /**< their channel sums */
    uint32_t awb_sum_g;
    uint32_t awb_sum_b;
    uint32_t isp_errors;        /**< the ISP's error interrupt bits raised */
    uint32_t bridge_errors;     /**< the CSI bridge's */
};

/** @brief One frame through the ISP: the input, the output, the blocks
 *         to run with their parameters, and the statistics wanted. */
struct tiku_p4_isp_job {
    const void *in;             /**< the RAW frame, 8-byte aligned */
    uint32_t width, height;     /**< pixels; width a multiple of 8, height
                                 *   even and at least 8 */
    uint8_t raw;                /**< TIKU_P4_ISP_RAW* */
    uint8_t bayer;              /**< TIKU_P4_ISP_BGGR .. RGGB */
    void *out;                  /**< the processed frame, 8-byte aligned */
    uint8_t out_format;         /**< TIKU_P4_ISP_OUT_*; OUT_RAW ignores the
                                 *   fields below */
    uint8_t yuv_bt709;          /**< 1: BT.709 instead of BT.601 */
    uint8_t yuv_limited;        /**< 1: limited range (16..235) */

    uint8_t bf_on;              /**< bilateral denoise */
    uint8_t bf_level;           /**< sigma, 0..63 */
    uint8_t bf_template[3][3];  /**< weights 0..15 */
    uint8_t sharpen_on;
    uint8_t sharpen_low;        /**< thresholds between flat, medium and
                                 *   high frequency */
    uint8_t sharpen_high;
    uint8_t sharpen_m_coeff;    /**< amounts in 3.5 fixed point (32 = 1.0) */
    uint8_t sharpen_h_coeff;
    uint8_t sharpen_template[3][3]; /**< 0..31 */
    uint8_t ccm_on;
    int32_t ccm[3][3];          /**< rows R', G', B' over R, G, B in 2.10
                                 *   fixed point: TIKU_P4_ISP_CCM_ONE = 1.0 */
    uint8_t gamma_on;
    uint8_t gamma[3][16];       /**< R, G, B curves: the output at inputs 16,
                                 *   32 .. 240, 255; the last must be 255 */
    uint8_t color_on;
    uint8_t contrast;           /**< 1.7 fixed point, 128 = 1.0 */
    uint8_t saturation;         /**< 1.7 fixed point, 128 = 1.0 */
    uint16_t hue;               /**< degrees, 0..359 */
    int8_t brightness;          /**< -128..127 */

    uint8_t ae_on;
    uint8_t ae_after_gamma;     /**< sample point 1 instead of 0 */
    struct tiku_p4_isp_window ae_window; /**< spans at least 5 x 5 pixels */
    uint8_t awb_on;
    uint8_t awb_after_ccm;
    struct tiku_p4_isp_window awb_window;
    uint16_t awb_lum_min, awb_lum_max;   /**< 0..1023 */
    uint16_t awb_rg_min, awb_rg_max;     /**< R/G in 2.8 fixed point */
    uint16_t awb_bg_min, awb_bg_max;     /**< B/G in 2.8 fixed point */
    struct tiku_p4_isp_stats *stats;    /**< filled after the frame, or NULL */
    uint8_t source;             /**< TIKU_P4_ISP_SRC_*; with CSI, in is not
                                 *   read and the frame is the next one the
                                 *   receiver passes on */
};

/** @brief Completion callback of an asynchronous frame, run from the
 *         DMA interrupt with TIKU_P4_ISP_OK or a negative error. */
typedef void (*tiku_p4_isp_done_t)(void *ctx, int rc);

/** @brief Bytes of a @p width x @p height input frame at @p raw depth. */
uint32_t tiku_p4_isp_in_bytes(uint32_t width, uint32_t height, uint8_t raw);
/** @brief Bytes of the output frame in @p out_format; 0 for
 *         TIKU_P4_ISP_OUT_RAW, whose size is the input's. */
uint32_t tiku_p4_isp_out_bytes(uint32_t width, uint32_t height,
                               uint8_t out_format);
/** @brief Run @p job and wait: TIKU_P4_ISP_OK or an error; on success
 *         the output holds the frame and job->stats, if given, the
 *         measurements.  A CSI job is refused with TIKU_P4_ISP_ERR_PARAM:
 *         its frame starts when the sensor streams, so it runs only
 *         asynchronously. */
int tiku_p4_isp_process(const struct tiku_p4_isp_job *job);
/** @brief Run @p job and leave it to the engines: @p done runs from the
 *         interrupt; until then the core leaves both buffers and @p job
 *         alone. */
int tiku_p4_isp_process_async(const struct tiku_p4_isp_job *job,
                              tiku_p4_isp_done_t done, void *ctx);
/** @brief End the frame in flight with TIKU_P4_ISP_ERR_TIMEOUT, its
 *         callback not run: for a CSI frame that never came.
 *  @return TIKU_P4_ISP_ERR_TIMEOUT, or TIKU_P4_ISP_OK when none was in
 *          flight. */
int tiku_p4_isp_abort(void);
/** @brief Frames completed and failed since boot. */
void tiku_p4_isp_counts(uint32_t *frames, uint32_t *failed);
#endif
