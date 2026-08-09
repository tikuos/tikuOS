/*
 * Tiku Operating System v0.06
 * Simple. Ubiquitous. Intelligence, Everywhere.
 * http://tiku-os.org
 *
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 *
 * tiku_drw_arch.c - RA8P1 2D drawing engine.
 *
 * Register mode (UM 63.7.1): the CPU sets every register, then the write to
 * ORIGIN starts the render.  Display-list mode is not used here.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include "tiku_drw_arch.h"

#include "tiku_ra8p1_regs.h"
#include "tiku_cpu_common.h"

/*
 * Limiter decision values are fixed point.  A hard-edged rectangle only needs
 * every inside pixel to reach the clamp ceiling, so this port scales by a
 * whole pixel step and lets the hardware saturate -- which makes the geometry
 * correct whatever the fractional width turns out to be.
 */
#define DRW_ONE                 (1L << 16)

/** @brief Bounded spin for the idle poll; the engine is far faster. */
#define DRW_WAIT_SPINS          2000000UL

/** @brief HWREVISION is never zero on a present engine, and never all ones. */
static uint32_t drw_id;

int
tiku_drw_arch_wait(void)
{
    uint32_t spins;

    for (spins = 0U; spins < DRW_WAIT_SPINS; spins++) {
        uint32_t st = TIKU_REG32(RA8P1_DRW_STATUS);

        if ((st & (RA8P1_DRW_ST_BUSYENUM | RA8P1_DRW_ST_BUSYWRITE)) == 0U) {
            return TIKU_DRW_OK;
        }
    }
    return TIKU_DRW_ERR_TIMEOUT;
}

/**
 * @brief Open or close the write protection over the power-domain control.
 *
 * @param unlock  Non-zero to allow writes, zero to protect again
 */
static void
drw_protect(int unlock)
{
    TIKU_REG16(RA8P1_PRCR_S) = (uint16_t)(RA8P1_PRCR_KEY |
                                          (unlock ? RA8P1_PRCR_PRC1 : 0U));
}

int
tiku_drw_arch_init(void)
{
    uint32_t id;
    uint32_t spins;

    if (drw_id != 0U) {
        return TIKU_DRW_OK;
    }

    /* The graphics power domain comes up gated, and the module stop is the
     * second gate, not the first: with the domain down the whole block reads
     * back zero and no fault is raised, so the order here is load-bearing.
     * PDCTRGD is written whole -- PDCSF and PDPGSF are read-only status in
     * the same byte and feeding one back has the write refused. */
    drw_protect(1);
    TIKU_REG8(RA8P1_PDCTRGD) = 0U;
    drw_protect(0);

    for (spins = 0U; spins < DRW_WAIT_SPINS; spins++) {
        uint8_t pd = TIKU_REG8(RA8P1_PDCTRGD);

        if ((pd & (RA8P1_PDCTRGD_PDCSF | RA8P1_PDCTRGD_PDPGSF)) == 0U) {
            break;
        }
    }
    if (spins == DRW_WAIT_SPINS) {
        return TIKU_DRW_ERR_STATE;
    }

    /* Only now the module stop, read-modify-written like the NPU path:
     * reserved bits here read as one and a computed mask that cleared them
     * would have the write refused. */
    TIKU_REG32(RA8P1_MSTPCRC) &= ~RA8P1_MSTPCRC_DRW;
    (void)TIKU_REG32(RA8P1_MSTPCRC);
    tiku_cpu_ra8p1_delay_us(30U);

    id = TIKU_REG32(RA8P1_DRW_HWREVISION);
    if (id == 0U || id == 0xFFFFFFFFUL) {
        return TIKU_DRW_ERR_STATE;
    }
    drw_id = id;
    return TIKU_DRW_OK;
}

uint32_t
tiku_drw_arch_id(void)
{
    return drw_id;
}

/**
 * @brief Expand a 565 pixel to the opaque 8-bit-per-channel colour COLOR1 wants.
 *
 * @param c  RGB565 source
 * @return ARGB8888 with alpha fully opaque
 */
static uint32_t
drw_argb_from_565(uint16_t c)
{
    uint32_t r = (uint32_t)((c >> 11) & 0x1FU);
    uint32_t g = (uint32_t)((c >> 5) & 0x3FU);
    uint32_t b = (uint32_t)(c & 0x1FU);

    /* Replicate the high bits into the low ones so full-scale stays full. */
    r = (r << 3) | (r >> 2);
    g = (g << 2) | (g >> 4);
    b = (b << 3) | (b >> 2);
    return 0xFF000000UL | (r << 16) | (g << 8) | b;
}

int
tiku_drw_arch_fill(void *fb, uint32_t pitch, uint32_t h,
                   uint32_t x, uint32_t y, uint32_t w, uint32_t rh,
                   uint16_t rgb565)
{
    uint32_t origin;

    if (drw_id == 0U) {
        return TIKU_DRW_ERR_STATE;
    }
    if (fb == 0 || w == 0U || rh == 0U) {
        return TIKU_DRW_ERR_INVALID;
    }
    /* Refuse geometry the engine would write outside the buffer; it has no
     * clipping of its own and a bad rectangle corrupts whatever follows. */
    if (x + w > pitch || y + rh > h) {
        return TIKU_DRW_ERR_INVALID;
    }
    if (tiku_drw_arch_wait() != TIKU_DRW_OK) {
        return TIKU_DRW_ERR_TIMEOUT;
    }

    /*
     * Four half planes, one per edge, in the bounding box's own coordinates:
     * the box IS the rectangle, so each decision value starts at or above the
     * ceiling and only leaves it outside the box.
     */
    TIKU_REG32(RA8P1_DRW_LSTART(0)) = (uint32_t)DRW_ONE;          /* left   */
    TIKU_REG32(RA8P1_DRW_LXADD(0))  = (uint32_t)DRW_ONE;
    TIKU_REG32(RA8P1_DRW_LYADD(0))  = 0U;

    TIKU_REG32(RA8P1_DRW_LSTART(1)) = (uint32_t)((long)w * DRW_ONE); /* right */
    TIKU_REG32(RA8P1_DRW_LXADD(1))  = (uint32_t)(-DRW_ONE);
    TIKU_REG32(RA8P1_DRW_LYADD(1))  = 0U;

    TIKU_REG32(RA8P1_DRW_LSTART(2)) = (uint32_t)DRW_ONE;          /* top    */
    TIKU_REG32(RA8P1_DRW_LXADD(2))  = 0U;
    TIKU_REG32(RA8P1_DRW_LYADD(2))  = (uint32_t)DRW_ONE;

    TIKU_REG32(RA8P1_DRW_LSTART(3)) = (uint32_t)((long)rh * DRW_ONE); /* bot */
    TIKU_REG32(RA8P1_DRW_LXADD(3))  = 0U;
    TIKU_REG32(RA8P1_DRW_LYADD(3))  = (uint32_t)(-DRW_ONE);

    /* COLOR1 is 8 bits per channel with its own alpha, whatever the
     * framebuffer format is: handing it a packed 565 word leaves alpha zero
     * and the fill is composited away to nothing. */
    TIKU_REG32(RA8P1_DRW_COLOR1)   = drw_argb_from_565(rgb565);
    TIKU_REG32(RA8P1_DRW_SIZE)     = (w & 0xFFFFU) | (rh << 16);
    TIKU_REG32(RA8P1_DRW_PITCH)    = pitch & 0xFFFFU;
    TIKU_REG32(RA8P1_DRW_CONTROL2) = RA8P1_DRW_CTL2_WRFMT_RGB565 |
                                     RA8P1_DRW_CTL2_OPAQUE;
    TIKU_REG32(RA8P1_DRW_CONTROL)  = RA8P1_DRW_CTL_LIMEN(0) |
                                     RA8P1_DRW_CTL_LIMEN(1) |
                                     RA8P1_DRW_CTL_LIMEN(2) |
                                     RA8P1_DRW_CTL_LIMEN(3);

    /* The bounding box's own corner, not the buffer's: the engine walks
     * SIZE pixels from whatever address ORIGIN names. */
    origin = (uint32_t)(uintptr_t)fb + ((y * pitch) + x) * 2U;

    __asm__ volatile ("dsb" ::: "memory");
    TIKU_REG32(RA8P1_DRW_ORIGIN) = origin;      /* starts the render */

    return tiku_drw_arch_wait();
}
