/*
 * Tiku Operating System v0.06
 * Simple. Persistent. Intelligent.
 * http://tiku-os.org
 *
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 *
 * tiku_ppa_arch.h - ESP32-P4 Pixel Processing Accelerator: fill, alpha
 * blend, and scale, rotate and mirror between pictures.  A picture's pixel
 * size names its format: 4 bytes ARGB8888 (B, G, R, A in memory), 3 bytes
 * RGB888 (B, G, R), 2 bytes RGB565 (little endian).
 *
 * SPDX-License-Identifier: Apache-2.0
 */
#ifndef TIKU_ESP32P4_PPA_ARCH_H_
#define TIKU_ESP32P4_PPA_ARCH_H_
#include <stdint.h>
#include "tiku_dma2d_arch.h"

#define TIKU_P4_PPA_OK           0
#define TIKU_P4_PPA_ERR_PARAM   -1   /**< NULL, format, size or range       */
#define TIKU_P4_PPA_ERR_BUSY    -2   /**< the 2D-DMA engine is taken        */
#define TIKU_P4_PPA_ERR_TIMEOUT -3   /**< no end of frame in time           */
#define TIKU_P4_PPA_ERR_FAULT   -4   /**< a DMA or scaler parameter error   */

/** @brief Quarter turns for tiku_p4_ppa_srm(), counter-clockwise. */
#define TIKU_P4_PPA_ROT_0    0u
#define TIKU_P4_PPA_ROT_90   1u
#define TIKU_P4_PPA_ROT_180  2u
#define TIKU_P4_PPA_ROT_270  3u
/** @brief Mirror flags for tiku_p4_ppa_srm(). */
#define TIKU_P4_PPA_MIRROR_X 1u
#define TIKU_P4_PPA_MIRROR_Y 2u
/** @brief A scale factor of 1, in sixteenths. */
#define TIKU_P4_PPA_SCALE_1  16u

/* An input's alpha, as tiku_p4_ppa_blend() takes it: kept, replaced by a
 * value, multiplied by value/256, or inverted.  Formats without alpha
 * start at 255. */
#define TIKU_P4_PPA_ALPHA_KEEP      0x000u
#define TIKU_P4_PPA_ALPHA_FIX(v)    (0x100u | ((v) & 0xFFu))
#define TIKU_P4_PPA_ALPHA_SCALE(v)  (0x200u | ((v) & 0xFFu))
#define TIKU_P4_PPA_ALPHA_INVERT    0x300u

/**
 * @brief Fill the @p w x @p h block at (@p x, @p y) of @p out with @p argb
 *        (0xAARRGGBB; RGB565 keeps each channel's top bits).
 * @return TIKU_P4_PPA_OK, or a negative TIKU_P4_PPA_ERR_*
 */
int tiku_p4_ppa_fill(const tiku_p4_picture_t *out, uint16_t x, uint16_t y,
                     uint16_t w, uint16_t h, uint32_t argb);

/** @brief The blend's inputs with a lookup table: the background, the
 *         foreground. */
#define TIKU_P4_PPA_BG 0u
#define TIKU_P4_PPA_FG 1u

/**
 * @brief Give blend input @p input a 256-entry table of ARGB8888 colours
 *        (0xAARRGGBB), or take it away with NULL.
 *
 * With a table, a 1-byte-per-pixel picture on that input is a picture of
 * indices into it.  The tables sit in the PPA and stay until replaced.
 *
 * @return TIKU_P4_PPA_OK or TIKU_P4_PPA_ERR_PARAM
 */
int tiku_p4_ppa_lut(unsigned input, const uint32_t *table);

/**
 * @brief Colour keys for tiku_p4_ppa_blend_keyed(): a pixel whose R, G and
 *        B each lie within its input's range (0xRRGGBB, low to high) is
 *        keyed.  A keyed foreground gives the background; a keyed
 *        background gives the background, or the foreground when
 *        @p reverse; both keyed give @p miss; neither blends.  An empty
 *        range (low above high) keys nothing.
 */
typedef struct {
    uint32_t fg_low, fg_high;
    uint32_t bg_low, bg_high;
    uint32_t miss;
    unsigned reverse;
} tiku_p4_ppa_keys_t;

/**
 * @brief Blend the @p w x @p h blocks of @p fg at (@p fx, @p fy) over @p bg
 *        at (@p bx, @p by) into @p out at (@p ox, @p oy).
 *
 * Alphas after @p bg_alpha and @p fg_alpha are applied, normalised to 1:
 * A = Ab + Af - Ab Af and C = (Cb Ab (1 - Af) + Cf Af) / A.  @p out may be
 * one of the inputs.  A 1-byte input needs its table (tiku_p4_ppa_lut()).
 *
 * @return TIKU_P4_PPA_OK, or a negative TIKU_P4_PPA_ERR_*
 */
int tiku_p4_ppa_blend(const tiku_p4_picture_t *out, uint16_t ox, uint16_t oy,
                      const tiku_p4_picture_t *bg, uint16_t bx, uint16_t by,
                      const tiku_p4_picture_t *fg, uint16_t fx, uint16_t fy,
                      uint16_t w, uint16_t h, uint16_t bg_alpha,
                      uint16_t fg_alpha);

/** @brief tiku_p4_ppa_blend() with colour keys @p keys deciding each pixel
 *         first. */
int tiku_p4_ppa_blend_keyed(const tiku_p4_picture_t *out, uint16_t ox,
                            uint16_t oy, const tiku_p4_picture_t *bg,
                            uint16_t bx, uint16_t by,
                            const tiku_p4_picture_t *fg, uint16_t fx,
                            uint16_t fy, uint16_t w, uint16_t h,
                            uint16_t bg_alpha, uint16_t fg_alpha,
                            const tiku_p4_ppa_keys_t *keys);

/**
 * @brief The block tiku_p4_ppa_srm() writes for a @p w x @p h input: each
 *        side times its scale (sixteenths, rounded down), swapped by a
 *        quarter or three-quarter turn.
 */
void tiku_p4_ppa_srm_size(uint16_t w, uint16_t h, uint16_t scale_x,
                          uint16_t scale_y, unsigned rotate, uint16_t *out_w,
                          uint16_t *out_h);

/**
 * @brief Scale the @p w x @p h block of @p in at (@p ix, @p iy) by
 *        @p scale_x / 16 and @p scale_y / 16 (1..4095 each, bilinear),
 *        turn it @p rotate quarter turns and mirror it per @p mirror, into
 *        @p out at (@p ox, @p oy).
 *
 * The pictures may differ in format but not in memory; the result, sized by
 * tiku_p4_ppa_srm_size(), must fit in @p out and be at most 8191 a side.
 *
 * @return TIKU_P4_PPA_OK, or a negative TIKU_P4_PPA_ERR_*
 */
int tiku_p4_ppa_srm(const tiku_p4_picture_t *out, uint16_t ox, uint16_t oy,
                    const tiku_p4_picture_t *in, uint16_t ix, uint16_t iy,
                    uint16_t w, uint16_t h, uint16_t scale_x, uint16_t scale_y,
                    unsigned rotate, unsigned mirror);

/** @brief The scaler's parameter-error flags from the last failed
 *         tiku_p4_ppa_srm() (SR_PARAM_ERR_ST), 0 after a success. */
uint32_t tiku_p4_ppa_srm_errors(void);

#endif /* TIKU_ESP32P4_PPA_ARCH_H_ */
