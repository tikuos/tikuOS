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
 * Limiter decision values are 16.16 fixed point.  A rectangle edge adds a
 * whole DRW_ONE per pixel, so every inside pixel saturates at full coverage
 * and the edges are hard.
 */
#define DRW_ONE                 (1L << 16)

/** @brief Iteration cap on the idle poll. */
#define DRW_WAIT_SPINS          2000000UL

/** @brief HWREVISION read at init; 0 until tiku_drw_arch_init() succeeds. */
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

    /* Power the graphics domain before releasing the module stop: with the
     * domain off the block reads back zero and raises no fault.  PDCTRGD is
     * written whole; a write that feeds back its read-only status bits
     * (PDCSF, PDPGSF) is refused. */
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

    /* Then the module stop, by read-modify-write: reserved bits read as one,
     * and a write that clears them is refused. */
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
 * @brief Expand an RGB565 colour to the opaque ARGB8888 that COLOR1 takes.
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
     * Four half planes, one per edge, in the bounding box's coordinates.  The
     * box is the rectangle, so each value starts at or above the ceiling and
     * drops below it only outside the box.
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

    /* COLOR1 is ARGB8888 whatever the framebuffer format; a raw 565 word
     * has alpha 0 and fills nothing. */
    TIKU_REG32(RA8P1_DRW_COLOR1)   = drw_argb_from_565(rgb565);
    TIKU_REG32(RA8P1_DRW_SIZE)     = (w & 0xFFFFU) | (rh << 16);
    TIKU_REG32(RA8P1_DRW_PITCH)    = pitch & 0xFFFFU;
    TIKU_REG32(RA8P1_DRW_CONTROL2) = RA8P1_DRW_CTL2_WRFMT_RGB565 |
                                     RA8P1_DRW_CTL2_OVER;
    TIKU_REG32(RA8P1_DRW_CONTROL)  = RA8P1_DRW_CTL_LIMEN(0) |
                                     RA8P1_DRW_CTL_LIMEN(1) |
                                     RA8P1_DRW_CTL_LIMEN(2) |
                                     RA8P1_DRW_CTL_LIMEN(3);

    /* ORIGIN is the box's top-left pixel; the engine walks SIZE pixels from
     * it. */
    origin = (uint32_t)(uintptr_t)fb + ((y * pitch) + x) * 2U;

    __asm__ volatile ("dsb" ::: "memory");
    TIKU_REG32(RA8P1_DRW_ORIGIN) = origin;      /* starts the render */

    return tiku_drw_arch_wait();
}

int
tiku_drw_arch_fill_circle(void *fb, uint32_t pitch, uint32_t h,
                          int32_t cx, int32_t cy, uint32_t r,
                          uint16_t rgb565)
{
    int32_t  bx, by, side;
    long     k, s2;
    uint32_t origin;

    if (drw_id == 0U) {
        return TIKU_DRW_ERR_STATE;
    }
    if (fb == 0 || r == 0U) {
        return TIKU_DRW_ERR_INVALID;
    }
    bx   = cx - (int32_t)r;
    by   = cy - (int32_t)r;
    side = (int32_t)(r * 2U);
    /* The engine does not clip, so the whole bounding square has to fit. */
    if (bx < 0 || by < 0 ||
        (uint32_t)(bx + side) > pitch || (uint32_t)(by + side) > h) {
        return TIKU_DRW_ERR_INVALID;
    }
    if (tiku_drw_arch_wait() != TIKU_DRW_OK) {
        return TIKU_DRW_ERR_TIMEOUT;
    }

    /*
     * A quadratic limiter evaluates a*x^2 + b*y^2 + c*x + d*y + f over the
     * bounding box (UM 63.6.2.2).  With the circle equation negated the value
     * is positive inside, the opaque side; the box is the circle's square, so
     * the centre is at (r, r): a = b = -1, c = d = 2r, f = -r^2.
     *
     * Near the edge the value grows by about 2r per pixel, so the scale
     * k = DRW_ONE / 2r reaches full coverage one pixel inside the edge.
     */
    k  = (long)DRW_ONE / (long)(r * 2U);
    s2 = (long)r * (long)r;

    TIKU_REG32(RA8P1_DRW_LSTART(0)) = (uint32_t)(-(k * s2));
    TIKU_REG32(RA8P1_DRW_LXADD(0))  = (uint32_t)(k * (long)(r * 2U - 1U));
    TIKU_REG32(RA8P1_DRW_LYADD(0))  = (uint32_t)(k * (long)(r * 2U - 1U));

    TIKU_REG32(RA8P1_DRW_LSTART(1)) = (uint32_t)(k * (long)(r * 2U - 1U));
    TIKU_REG32(RA8P1_DRW_LXADD(1))  = (uint32_t)(-(2L * k));
    TIKU_REG32(RA8P1_DRW_LYADD(1))  = (uint32_t)(-(2L * k));

    TIKU_REG32(RA8P1_DRW_COLOR1)   = drw_argb_from_565(rgb565);
    TIKU_REG32(RA8P1_DRW_SIZE)     = ((uint32_t)side & 0xFFFFU) |
                                     ((uint32_t)side << 16);
    TIKU_REG32(RA8P1_DRW_PITCH)    = pitch & 0xFFFFU;
    TIKU_REG32(RA8P1_DRW_CONTROL2) = RA8P1_DRW_CTL2_WRFMT_RGB565 |
                                     RA8P1_DRW_CTL2_OVER;
    TIKU_REG32(RA8P1_DRW_CONTROL)  = RA8P1_DRW_CTL_LIMEN(0) |
                                     RA8P1_DRW_CTL_QUAD1;

    origin = (uint32_t)(uintptr_t)fb +
             (((uint32_t)by * pitch) + (uint32_t)bx) * 2U;

    __asm__ volatile ("dsb" ::: "memory");
    TIKU_REG32(RA8P1_DRW_ORIGIN) = origin;      /* starts the render */

    return tiku_drw_arch_wait();
}
