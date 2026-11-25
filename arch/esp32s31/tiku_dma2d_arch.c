/*
 * Tiku Operating System v0.06
 * Simple. Persistent. Intelligent.
 * http://tiku-os.org
 *
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 *
 * tiku_dma2d_arch.c - One bring-up of the S31 2D-DMA for every engine that
 * feeds through it.  The sequence is ESP-IDF's dma2d_ll.h for the S31.
 *
 * SPDX-License-Identifier: Apache-2.0
 */
#include "tiku_dma2d_arch.h"

/* Register access; a host model defines its own before including this file. */
#ifndef TIKU_S31_DMA2D_REG_READ
#define TIKU_S31_DMA2D_REG_READ(a) (*(volatile uint32_t *)(uintptr_t)(a))
#define TIKU_S31_DMA2D_REG_WRITE(a, v) (*(volatile uint32_t *)(uintptr_t)(a) = (v))
#endif

/* HP_SYS_CLKRST: the clock (bit 0) and reset (bit 1); HP_SYSTEM: the
 * memory's power control. */
#define CLK_DMA2D       0x205870B0u
#define CLK_EN          (1u << 0)
#define RST_EN          (1u << 1)
#define MEM_DMA2D       0x20586210u
#define MEM_AWAKE       2u              /* lp_mode 2, lp_en and force_ctrl 0 */
/* TEE master 81, the 2D-DMA, reads and writes SRAM and PSRAM only in
 * trusted mode (mode 0); the boot leaves it in mode 3. */
#define TEE_DMA2D       0x20504144u
#define TEE_MODE        3u
#define TEE_LOCK        (1u << 2)
#define RST_CONF        (TIKU_S31_DMA2D_BASE + 0xA04u)
#define OUT_ARB_CONFIG  (TIKU_S31_DMA2D_BASE + 0xA18u)
#define IN_ARB_CONFIG   (TIKU_S31_DMA2D_BASE + 0xA1Cu)
#define RST_AXI_RD      (1u << 0)
#define RST_AXI_WR      (1u << 1)
#define RST_CLK_EN      (1u << 2)
#define CONF0           0x00u
#define LINK_CONF       0x1Cu
#define STATE           0x24u
#define CHAN_RST        (1u << 24)
#define CMD_DISABLE     (1u << 25)
#define OUT_STOP        (1u << 20)
#define IN_STOP         (1u << 21)
#define OUT_RESET_AVAIL (1u << 24)
#define IN_RESET_AVAIL  (1u << 23)

static uint8_t engine_ready;

static void set_bits(uint32_t reg, uint32_t bits, uint32_t value)
{
    TIKU_S31_DMA2D_REG_WRITE(reg, (TIKU_S31_DMA2D_REG_READ(reg) & ~bits) | (value & bits));
}

int tiku_s31_dma2d_init(void)
{
    if (engine_ready) return 0;
    uint32_t mode = TIKU_S31_DMA2D_REG_READ(TEE_DMA2D);

    if ((mode & TEE_LOCK) && (mode & TEE_MODE)) return -1;
    TIKU_S31_DMA2D_REG_WRITE(TEE_DMA2D, mode & ~TEE_MODE);
    if (TIKU_S31_DMA2D_REG_READ(TEE_DMA2D) & TEE_MODE) return -1;
    set_bits(CLK_DMA2D, CLK_EN, CLK_EN);
    set_bits(CLK_DMA2D, RST_EN, RST_EN);
    set_bits(CLK_DMA2D, RST_EN, 0u);
    set_bits(MEM_DMA2D, 0xFu, MEM_AWAKE);
    set_bits(RST_CONF, RST_CLK_EN, RST_CLK_EN);
    set_bits(RST_CONF, RST_AXI_RD, RST_AXI_RD);
    set_bits(RST_CONF, RST_AXI_RD, 0u);
    set_bits(RST_CONF, RST_AXI_WR, RST_AXI_WR);
    set_bits(RST_CONF, RST_AXI_WR, 0u);
    TIKU_S31_DMA2D_REG_WRITE(OUT_ARB_CONFIG, 0u);
    TIKU_S31_DMA2D_REG_WRITE(IN_ARB_CONFIG, 0u);
    engine_ready = 1u;
    return 0;
}

int tiku_s31_dma2d_chan_reset(uint32_t ch, int in)
{
    uint32_t avail = in ? IN_RESET_AVAIL : OUT_RESET_AVAIL;
    unsigned n;

    set_bits(ch + LINK_CONF, in ? IN_STOP : OUT_STOP, ~0u);
    set_bits(ch + CONF0, CMD_DISABLE, CMD_DISABLE);
    for (n = 0u; !(TIKU_S31_DMA2D_REG_READ(ch + STATE) & avail); n++) {
        if (n == 100000u) {
            set_bits(ch + CONF0, CMD_DISABLE, 0u);
            return -1;
        }
    }
    set_bits(ch + CONF0, CHAN_RST, CHAN_RST);
    set_bits(ch + CONF0, CHAN_RST, 0u);
    set_bits(ch + CONF0, CMD_DISABLE, 0u);
    return 0;
}
