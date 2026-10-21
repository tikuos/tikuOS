/*
 * Tiku Operating System v0.06
 * Simple. Persistent. Intelligent.
 * http://tiku-os.org
 *
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 *
 * tiku_dma2d_arch.h - ESP32-P4 2D-DMA: rectangle copies between pictures,
 * and the channel layer the PPA and the JPEG codec move pixels through.
 *
 * Either picture may be in HP SRAM or the attached PSRAM; the caches are
 * kept coherent for both.  One transfer holds the engine at a time.
 *
 * SPDX-License-Identifier: Apache-2.0
 */
#ifndef TIKU_ESP32P4_DMA2D_ARCH_H_
#define TIKU_ESP32P4_DMA2D_ARCH_H_
#include <stdint.h>

#define TIKU_P4_DMA2D_OK           0
#define TIKU_P4_DMA2D_ERR_PARAM   -1   /**< NULL, size, range or memory     */
#define TIKU_P4_DMA2D_ERR_BUSY    -2   /**< another transfer holds the engine */
#define TIKU_P4_DMA2D_ERR_TIMEOUT -3   /**< no end of frame in time         */
#define TIKU_P4_DMA2D_ERR_FAULT   -4   /**< descriptor, FIFO or peripheral error */

/** @brief The largest picture side, in pixels. */
#define TIKU_P4_DMA2D_SIDE_MAX 16383u

/** @brief A picture: @p height lines of @p width pixels, @p bpp bytes each,
 *         one line straight after another from @p base. */
typedef struct {
    void    *base;
    uint16_t width;
    uint16_t height;
    uint8_t  bpp;          /**< 1, 2, 3 or 4 */
} tiku_p4_picture_t;

/**
 * @brief Copy the @p w x @p h block at (@p sx, @p sy) of @p src to
 *        (@p dx, @p dy) of @p dst.
 *
 * Both pictures have the same pixel size, and the block fits in each.  The
 * destination must not share 64-byte cache lines with data the core writes
 * during the copy.
 *
 * @return TIKU_P4_DMA2D_OK, or a negative TIKU_P4_DMA2D_ERR_*
 */
int tiku_p4_dma2d_copy(const tiku_p4_picture_t *dst, uint16_t dx, uint16_t dy,
                       const tiku_p4_picture_t *src, uint16_t sx, uint16_t sy,
                       uint16_t w, uint16_t h);

/* Conversions a copy applies on the way: pixel bytes before and after,
 * and the formats.  RGB888 is B, G, R in memory; YUV444 is Y, U, V; YUV422
 * is U, Y, V, Y for each pair of pixels.  YUV is BT.601 limited range
 * (Y 16..235), as the engine's own tables have it. */
#define TIKU_P4_DMA2D_CONV_NONE          0u
#define TIKU_P4_DMA2D_CONV_RGB888_RGB565 1u   /**< 3 to 2 bytes, top bits kept */
#define TIKU_P4_DMA2D_CONV_RGB565_RGB888 2u   /**< 2 to 3: a channel's low bits
                                               *   repeated below it */
#define TIKU_P4_DMA2D_CONV_RGB888_YUV444 3u   /**< 3 to 3: V, U, Y in memory */
#define TIKU_P4_DMA2D_CONV_RGB888_YUV422 4u   /**< 3 to 2: U, Y, V, Y a pair, U
                                               *   of its first pixel, V of its
                                               *   second */
#define TIKU_P4_DMA2D_CONV_YUV444_RGB888 5u   /**< 3 to 3 from V, U, Y */
#define TIKU_P4_DMA2D_CONV_YUV422_RGB888 6u   /**< 2 to 3 from U, Y, V, Y */
#define TIKU_P4_DMA2D_CONV_SWAP_RB       7u   /**< 3 to 3: B, G, R to R, G, B */

/**
 * @brief Copy a block as tiku_p4_dma2d_copy() does, converting each pixel
 *        by @p conv on the way; the pictures' pixel sizes are the
 *        conversion's.  A 4:2:2 block starts and ends on an even column.
 * @return TIKU_P4_DMA2D_OK, or a negative TIKU_P4_DMA2D_ERR_*
 */
int tiku_p4_dma2d_convert(const tiku_p4_picture_t *dst, uint16_t dx,
                          uint16_t dy, const tiku_p4_picture_t *src,
                          uint16_t sx, uint16_t sy, uint16_t w, uint16_t h,
                          unsigned conv);

/** @brief Copies finished and copies that failed since boot. */
void tiku_p4_dma2d_stats(uint32_t *done, uint32_t *failed);

/*---------------------------------------------------------------------------*/
/* The channel layer, for the PPA and JPEG drivers                           */
/*---------------------------------------------------------------------------*/

/** @brief A descriptor: 24 bytes, 8-aligned on the P4. */
typedef struct {
    uint32_t dw0;          /**< block height [13:0], width [27:14], flags */
    uint32_t dw1;          /**< picture height [13:0], width [27:14], pixel code */
    uint32_t dw2;          /**< block y [13:0], x [27:14], mode [28] */
    void    *buf;
    void    *next;
    uint32_t pad;
} tiku_p4_dma2d_desc_t;

/* CONF0 bits a peripheral's channel adds; every channel bursts descriptors,
 * moves data in 64-byte bursts and keeps them inside page bounds.  Exactly
 * one TIKU_P4_DMA2D_MB_* goes with each. */
#define TIKU_P4_DMA2D_MEM_TRANS  (1u << 0)    /**< in: from the same-numbered out */
#define TIKU_P4_DMA2D_MB_8X8     (0u << 9)    /**< 8x8 macro blocks */
#define TIKU_P4_DMA2D_MB_16X8    (1u << 9)    /**< 16 wide, 8 high */
#define TIKU_P4_DMA2D_MB_16X16   (2u << 9)
#define TIKU_P4_DMA2D_MB_NONE    (3u << 9)    /**< no macro-block ordering */
#define TIKU_P4_DMA2D_DSCR_PORT  (1u << 11)   /**< the peripheral sizes the block */
#define TIKU_P4_DMA2D_REORDER    (1u << 16)   /**< macro-block reorder, channel 0 */

/* Descriptor word 0 flags and word 2's mode. */
#define TIKU_P4_DMA2D_D0_OWNER   (1u << 31)   /**< the engine owns it */
#define TIKU_P4_DMA2D_D0_SUC_EOF (1u << 30)   /**< the frame ends with it */
#define TIKU_P4_DMA2D_D0_2D      (1u << 29)
#define TIKU_P4_DMA2D_D2_MULTI   (1u << 28)   /**< every block of the picture */

/* In channel 0's colour converter, IN_COLOR_CONVERT values. */
#define TIKU_P4_DMA2D_CSC_YUV_RGB888 0x05u    /**< YUV 4:2:x to RGB888, matrix */
#define TIKU_P4_DMA2D_CSC_YUV_RGB565 0x04u    /**< YUV 4:2:x to RGB565, matrix */
#define TIKU_P4_DMA2D_CSC_NONE_IN    0x39u    /**< straight through */

/** @brief 1 when all @p n bytes from @p base are in HP SRAM or the attached
 *         PSRAM, where the engine reaches; else 0. */
int tiku_p4_dma2d_reachable(const void *base, uint32_t n);
/** @brief 1 when the engine can take @p p: its sides, its pixel size, and
 *         every byte reachable; else 0. */
int tiku_p4_dma2d_picture_ok(const tiku_p4_picture_t *p);
/** @brief Fill @p d for the @p w x @p h block at (@p x, @p y) of @p p: 2D,
 *         one block, end of frame, owned by the engine. */
void tiku_p4_dma2d_desc_block(tiku_p4_dma2d_desc_t *d,
                              const tiku_p4_picture_t *p, uint16_t x,
                              uint16_t y, uint16_t w, uint16_t h);
/** @brief Take the engine for one transfer, bringing it up the first time.
 *  @return TIKU_P4_DMA2D_OK or TIKU_P4_DMA2D_ERR_BUSY */
int tiku_p4_dma2d_claim(void);
/** @brief Hand the engine back after tiku_p4_dma2d_claim(). */
void tiku_p4_dma2d_unclaim(void);
/**
 * @brief Reset out channel @p ch (0..2), select peripheral @p peri, set
 *        CONF0 to the defaults plus @p conf and point it at @p d.
 * @return TIKU_P4_DMA2D_OK, or _ERR_FAULT when the channel never idled
 */
int tiku_p4_dma2d_out_open(unsigned ch, unsigned peri, uint32_t conf,
                           const tiku_p4_dma2d_desc_t *d);
/** @brief The same for in channel @p ch (0..1); its descriptors are
 *         handed back to the CPU. */
int tiku_p4_dma2d_in_open(unsigned ch, unsigned peri, uint32_t conf,
                          const tiku_p4_dma2d_desc_t *d);
/** @brief The block out channel @p ch fetches per request when its
 *         peripheral sizes blocks (TIKU_P4_DMA2D_DSCR_PORT): @p w x @p h. */
void tiku_p4_dma2d_out_port_block(unsigned ch, uint16_t w, uint16_t h);
/** @brief Set in channel 0's colour converter to @p csc with matrix
 *         @p params (six IN_COLOR_PARAM words; NULL: BT.601 limited range)
 *         and no byte reordering. */
void tiku_p4_dma2d_in0_csc(uint32_t csc, const uint32_t *params);
/** @brief In channel @p ch's raw interrupt bits: 1 end of frame, 2-10
 *         errors, 10 the last descriptor full. */
uint32_t tiku_p4_dma2d_in_raw(unsigned ch);
/** @brief Start out channel @p ch, or in channel @p ch. */
void tiku_p4_dma2d_out_start(unsigned ch);
void tiku_p4_dma2d_in_start(unsigned ch);
/**
 * @brief Wait up to @p budget_us for in channel @p ch's end of frame.
 * @return TIKU_P4_DMA2D_OK, _ERR_FAULT on an error it reported, or
 *         _ERR_TIMEOUT
 */
int tiku_p4_dma2d_in_wait(unsigned ch, uint32_t budget_us);
/** @brief Unselect the out channels in mask @p outs and the in channels in
 *         @p ins, stopping them first when @p stop. */
void tiku_p4_dma2d_close(unsigned outs, unsigned ins, int stop);

#endif /* TIKU_ESP32P4_DMA2D_ARCH_H_ */
