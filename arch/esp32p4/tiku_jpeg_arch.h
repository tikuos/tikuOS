/*
 * Tiku Operating System v0.06
 * Simple. Persistent. Intelligent.
 * http://tiku-os.org
 *
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 *
 * tiku_jpeg_arch.h - ESP32-P4 JPEG codec: baseline encode and decode.
 *
 * Pictures are RGB888 (3 bytes: B, G, R), RGB565 (2 bytes, little endian)
 * or grey (1 byte), by their pixel size.  Each call returns once the result
 * is in memory; the 2D-DMA engine is held meanwhile.
 *
 * SPDX-License-Identifier: Apache-2.0
 */
#ifndef TIKU_ESP32P4_JPEG_ARCH_H_
#define TIKU_ESP32P4_JPEG_ARCH_H_
#include <stdint.h>
#include "tiku_dma2d_arch.h"

#define TIKU_P4_JPEG_OK            0
#define TIKU_P4_JPEG_ERR_PARAM    -1   /**< NULL, size, format or alignment   */
#define TIKU_P4_JPEG_ERR_BUSY     -2   /**< the 2D-DMA engine is taken        */
#define TIKU_P4_JPEG_ERR_TIMEOUT  -3   /**< no end of frame in time           */
#define TIKU_P4_JPEG_ERR_FAULT    -4   /**< the codec or the DMA reported one */
#define TIKU_P4_JPEG_ERR_FORMAT   -5   /**< a file this decoder does not take */
#define TIKU_P4_JPEG_ERR_ROOM     -6   /**< the output does not fit           */

/** @brief Chroma sampling: full, halved across, halved both ways. */
#define TIKU_P4_JPEG_444 0u
#define TIKU_P4_JPEG_422 1u
#define TIKU_P4_JPEG_420 2u

/** @brief Header bytes tiku_p4_jpeg_encode() writes before the scan, for a
 *         colour and a grey picture: whole 64-byte cache lines. */
#define TIKU_P4_JPEG_HEADER_COLOR 640u
#define TIKU_P4_JPEG_HEADER_GRAY  384u

/**
 * @brief Encode @p in at @p quality (1..100) into @p out, @p cap bytes, and
 *        set *@p len to the file's size: a JFIF with the standard tables.
 *
 * Grey pictures ignore @p sampling.  The sides are whole MCUs, 8 pixels or
 * 16 where the chroma is halved; @p out starts a cache line.
 *
 * @return TIKU_P4_JPEG_OK, or a negative TIKU_P4_JPEG_ERR_*
 */
int tiku_p4_jpeg_encode(const tiku_p4_picture_t *in, unsigned sampling,
                        unsigned quality, uint8_t *out, uint32_t cap,
                        uint32_t *len);

/**
 * @brief Encode @p in, a YUV422 picture (U, Y, V, Y for each pair of
 *        pixels, 2 bytes a pixel, JFIF full range) at 4:2:2, as
 *        tiku_p4_jpeg_encode() does otherwise.
 * @return TIKU_P4_JPEG_OK, or a negative TIKU_P4_JPEG_ERR_*
 */
int tiku_p4_jpeg_encode_yuv422(const tiku_p4_picture_t *in, unsigned quality,
                               uint8_t *out, uint32_t cap, uint32_t *len);

/**
 * @brief Decode a colour file to its own samples, JFIF full range: a 4:4:4
 *        file into a 3-byte picture (V, U, Y in memory), a 4:2:2 file into
 *        a 2-byte one (U, Y, V, Y pairs), without the conversion to RGB;
 *        otherwise as tiku_p4_jpeg_decode().
 * @return TIKU_P4_JPEG_OK, _ERR_FORMAT for a 4:2:0 or grey file, or a
 *         negative TIKU_P4_JPEG_ERR_*
 */
int tiku_p4_jpeg_decode_yuv(const uint8_t *jpg, uint32_t len,
                            const tiku_p4_picture_t *out);

/** @brief What a file holds: its size, the size padded to whole MCUs (the
 *         picture tiku_p4_jpeg_decode() writes), its components and its
 *         chroma sampling. */
typedef struct {
    uint16_t width;
    uint16_t height;
    uint16_t pad_width;
    uint16_t pad_height;
    uint8_t  components;   /**< 1 grey, 3 colour */
    uint8_t  sampling;     /**< TIKU_P4_JPEG_444, _422 or _420 */
} tiku_p4_jpeg_info_t;

/**
 * @brief Read the headers of the @p len-byte file @p jpg.
 * @return TIKU_P4_JPEG_OK, or _ERR_FORMAT for anything but a baseline file
 *         of one or three components with a supported sampling
 */
int tiku_p4_jpeg_info(const uint8_t *jpg, uint32_t len,
                      tiku_p4_jpeg_info_t *info);

/**
 * @brief Decode the @p len-byte file @p jpg into @p out.
 *
 * @p out is pad_width x pad_height pixels (tiku_p4_jpeg_info()), RGB888 or
 * RGB565 for a colour file and grey for a grey one, its first byte on a
 * cache line.  The file ends with its EOI marker.
 *
 * @return TIKU_P4_JPEG_OK, or a negative TIKU_P4_JPEG_ERR_*
 */
int tiku_p4_jpeg_decode(const uint8_t *jpg, uint32_t len,
                        const tiku_p4_picture_t *out);

/** @brief The codec's interrupt flags from the last call that failed with
 *         TIKU_P4_JPEG_ERR_FAULT, 0 after a success. */
uint32_t tiku_p4_jpeg_errors(void);

#endif /* TIKU_ESP32P4_JPEG_ARCH_H_ */
