/*
 * Tiku Operating System v0.06
 * Simple. Persistent. Intelligent.
 * http://tiku-os.org
 *
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 *
 * tiku_h264_arch.h - ESP32-P4 H.264 encoder: baseline-profile streams
 * (IDR and P frames, one reference, CAVLC) from packed 4:2:0 frames, one
 * encoder at a time.  A frame is 1.5 bytes a pixel: each row holds a
 * chroma byte then two luma bytes a pair, U on even rows, V on odd.
 *
 * SPDX-License-Identifier: Apache-2.0
 */
#ifndef TIKU_ESP32P4_H264_ARCH_H_
#define TIKU_ESP32P4_H264_ARCH_H_
#include <stdint.h>

#define TIKU_P4_H264_OK            0
#define TIKU_P4_H264_ERR_PARAM    -1   /**< NULL, size, alignment, QP, GOP   */
#define TIKU_P4_H264_ERR_STATE    -2   /**< not open, or open already         */
#define TIKU_P4_H264_ERR_BUSY     -3   /**< a frame is in flight              */
#define TIKU_P4_H264_ERR_TIMEOUT  -4   /**< no frame end in time              */
#define TIKU_P4_H264_ERR_ROOM     -5   /**< the output buffer is too small    */
#define TIKU_P4_H264_ERR_FAULT    -6   /**< the DMA did not come back         */

#define TIKU_P4_H264_MIN_SIDE    80u
#define TIKU_P4_H264_MAX_WIDTH   1920u
#define TIKU_P4_H264_MAX_HEIGHT  2032u
#define TIKU_P4_H264_QP_MAX      51u

/** @brief Bytes of a width x height frame as the encoder reads it. */
#define TIKU_P4_H264_FRAME_BYTES(w, h) ((uint32_t)(w) * (h) * 3u / 2u)

/** @brief What to encode: sides whole macroblocks within the limits, an
 *         IDR every @p gop frames, QP @p qp_min..@p qp_max (equal for a
 *         fixed QP, else rate control toward @p bitrate bits a second). */
typedef struct {
    uint16_t width;
    uint16_t height;
    uint8_t  fps;
    uint8_t  gop;
    uint8_t  qp_min;
    uint8_t  qp_max;
    uint32_t bitrate;
} tiku_p4_h264_cfg_t;

/** @brief The encoder's own memory: @p sram of tiku_p4_h264_sram_bytes()
 *         in HP SRAM (the reference strip, deblocking scratch and
 *         descriptors), @p store of tiku_p4_h264_store_bytes() anywhere
 *         the DMA reaches (the deblocked frame); both 16-byte aligned and
 *         left to the encoder until it closes. */
typedef struct {
    uint8_t *sram;
    uint32_t sram_bytes;
    uint8_t *store;
    uint32_t store_bytes;
} tiku_p4_h264_work_t;

uint32_t tiku_p4_h264_sram_bytes(uint16_t width);
uint32_t tiku_p4_h264_store_bytes(uint16_t width, uint16_t height);

/** @brief Bring the encoder up for @p cfg with @p work.
 * @return TIKU_P4_H264_OK, _ERR_PARAM, or _ERR_STATE when open already */
int tiku_p4_h264_open(const tiku_p4_h264_cfg_t *cfg,
                      const tiku_p4_h264_work_t *work);
/** @brief Stop the engine and release @p work; a repeat does nothing. */
void tiku_p4_h264_close(void);

/**
 * @brief Encode @p frame (16-byte aligned) into @p out, @p cap bytes
 *        (8-byte aligned), and set *@p len to the stream's length and
 *        *@p keyframe when it is an IDR with its SPS and PPS in front.
 *
 * An Annex B stream at @p out, then 8 to 15 zero bytes inside @p cap; a
 * frame that fails leaves no usable stream and makes the next an IDR.
 *
 * @return TIKU_P4_H264_OK, or a negative TIKU_P4_H264_ERR_*
 */
int tiku_p4_h264_encode(const uint8_t *frame, uint8_t *out, uint32_t cap,
                        uint32_t *len, int *keyframe);

typedef void (*tiku_p4_h264_done_t)(void *ctx, int rc);

/** @brief tiku_p4_h264_encode() left to the encoder: returns once the
 *         frame runs; @p done is called from the completion interrupt
 *         with the result, *@p len and *@p keyframe written before it. */
int tiku_p4_h264_encode_async(const uint8_t *frame, uint8_t *out,
                              uint32_t cap, uint32_t *len, int *keyframe,
                              tiku_p4_h264_done_t done, void *ctx);

/** @brief Make the next frame an IDR. */
void tiku_p4_h264_force_idr(void);
/** @brief Frames encoded, IDRs among them, stream bytes, failed frames. */
void tiku_p4_h264_counts(uint32_t *frames, uint32_t *keyframes,
                         uint32_t *bytes, uint32_t *errors);
#endif /* TIKU_ESP32P4_H264_ARCH_H_ */
