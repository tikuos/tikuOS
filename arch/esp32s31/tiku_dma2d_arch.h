/*
 * Tiku Operating System v0.06
 * Simple. Persistent. Intelligent.
 * http://tiku-os.org
 *
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 *
 * tiku_dma2d_arch.h - The S31 2D-DMA engine's bring-up and channel reset,
 * shared by the PPA and the JPEG codec, and the picture both describe.
 *
 * SPDX-License-Identifier: Apache-2.0
 */
#ifndef TIKU_S31_DMA2D_ARCH_H_
#define TIKU_S31_DMA2D_ARCH_H_
#include <stdint.h>

/** @brief A picture: rows of @p width pixels of @p bpp bytes, no gaps, in
 *         internal SRAM or PSRAM.  4 bytes is ARGB8888 (B, G, R, A in
 *         memory), 2 bytes RGB565 (little endian) or UYVY. */
typedef struct {
    void *base;
    uint16_t width, height;
    uint8_t bpp;
} tiku_s31_picture_t;

#define TIKU_S31_DMA2D_BASE 0x20346000u
#define TIKU_S31_DMA2D_TX(n) (TIKU_S31_DMA2D_BASE + 0x100u * (n))
#define TIKU_S31_DMA2D_RX(n) (TIKU_S31_DMA2D_BASE + 0x500u + 0x100u * (n))

/** @brief Give the 2D-DMA trusted access to memory, clock it, pulse its
 *  reset, power its memory and empty its AXI FIFOs, the first time only.
 *  @return 0, or -1 when the boot locked it out of trusted mode */
int tiku_s31_dma2d_init(void);

/** @brief Stop the channel at @p ch, a TX or (@p in 1) RX base, and reset
 *  it once the engine allows.  @return 0, or -1 when it never did */
int tiku_s31_dma2d_chan_reset(uint32_t ch, int in);
#endif
