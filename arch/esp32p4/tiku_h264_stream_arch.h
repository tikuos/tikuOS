/*
 * Tiku Operating System v0.06
 * Simple. Persistent. Intelligent.
 * http://tiku-os.org
 *
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 *
 * tiku_h264_stream_arch.h - P4 H.264: the parameter sets and slice headers
 * the hardware leaves to software, and the frame-level rate controller.
 *
 * SPDX-License-Identifier: Apache-2.0
 */
#ifndef TIKU_ESP32P4_H264_STREAM_ARCH_H_
#define TIKU_ESP32P4_H264_STREAM_ARCH_H_
#include <stdint.h>

/* Each writer puts an Annex B start code and one NAL unit at @p buf and
 * returns the bits used, the start code's 32 among them, or 0 when @p cap
 * bytes do not hold it.  Emulation prevention bytes are inserted. */

/** @brief A baseline-profile SPS: @p width x @p height (whole
 *         macroblocks), one reference frame, @p fps in the VUI timing. */
uint32_t tiku_p4_h264_sps(uint8_t *buf, uint32_t cap, uint16_t width,
                          uint16_t height, uint8_t fps);
/** @brief The PPS: CAVLC, pic_init_qp @p qp, the deblocking filter
 *         controlled from the slice header. */
uint32_t tiku_p4_h264_pps(uint8_t *buf, uint32_t cap, uint8_t qp);
/** @brief A slice header without its trailing bits, the start code a
 *         0xFF placeholder the encoder rewrites: an IDR I slice or a P
 *         slice, @p frame_num, slice_qp_delta @p qp_delta. */
uint32_t tiku_p4_h264_slice(uint8_t *buf, uint32_t cap, int idr,
                            uint8_t frame_num, int8_t qp_delta);

/** @brief Frame-level rate control toward a bit rate: the QP of each
 *         frame from the last four frames' bits and mean absolute
 *         differences, the error integrated and clipped. */
typedef struct {
    uint8_t  qp_min, qp_max;
    int8_t   qpm, eqp;
    uint8_t  qp_average;
    uint8_t  frame;
    uint16_t mb_count;
    uint32_t target_bits;
    uint32_t bits_per_frame;
    uint32_t bits_last[4];
    uint32_t bits_last_average;
    float    mad[4];
    float    mad_last_average;
    float    mad_pred;
    float    err_sum;
    int64_t  ebits;
    int64_t  ebits_clip;
} tiku_p4_h264_rc_t;

/** @brief Start controlling @p mb_count macroblocks a frame at @p fps
 *         toward @p bitrate bits a second within [@p qp_min, @p qp_max]. */
void tiku_p4_h264_rc_init(tiku_p4_h264_rc_t *rc, uint8_t qp_min,
                          uint8_t qp_max, uint32_t bitrate, uint8_t fps,
                          uint16_t mb_count);
/** @brief Before a frame: its QP, and the rate and predicted MAD words
 *         the encoder's rate-control registers take. */
void tiku_p4_h264_rc_start(tiku_p4_h264_rc_t *rc, int idr, uint32_t *rate,
                           uint32_t *pred_mad, uint8_t *qp);
/** @brief After a frame: the bits it took, the sum of its QPs and of its
 *         MADs, as the encoder's status registers report them. */
void tiku_p4_h264_rc_end(tiku_p4_h264_rc_t *rc, uint32_t bits,
                         uint32_t qp_sum, uint32_t mad_sum);
#endif /* TIKU_ESP32P4_H264_STREAM_ARCH_H_ */
