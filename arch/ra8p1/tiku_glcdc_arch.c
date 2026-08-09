/*
 * Tiku Operating System v0.06
 * Simple. Ubiquitous. Intelligence, Everywhere.
 * http://tiku-os.org
 *
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 *
 * tiku_glcdc_arch.c - RA8P1 graphics LCD controller.
 *
 * Background-plane timing plus one graphics layer, in register order: power
 * domain, module stop, LCDCLK, timing, layer, then the reflect bits that make
 * any of it take effect.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include "tiku_glcdc_arch.h"

#include "tiku_ra8p1_regs.h"
#include "tiku_cpu_common.h"

/** @brief LCDCLK as this driver leaves it: MOCO, undivided. */
#define GLCDC_PIXEL_HZ          8000000UL

/* Panel-clock divider.  The value the Renesas demo runs on this board, kept
 * because it is known to produce a pixel clock here; the tests check the frame
 * rate as a ratio between two periods, so they do not depend on it. */
#define GLCDC_PANEL_DCDR        7U

/** @brief Bounded spin for the clock and enable handshakes. */
#define GLCDC_WAIT_SPINS        1000000UL

/** @brief Every timing field is 11 bits wide (UM 64.2). */
#define GLCDC_FIELD_MAX         2047U

static uint8_t glcdc_running;

/**
 * @brief Open or close the write protection over the power-domain control.
 *
 * @param unlock  Non-zero to allow writes, zero to protect again
 */
static void
glcdc_protect(int unlock)
{
    /* PRC0 covers the clock generation circuit and PRC1 the power domain;
     * this driver touches both, and a write made without the matching bit
     * open is dropped rather than refused, so the symptom is a handshake
     * that never completes. */
    TIKU_REG16(RA8P1_PRCR_S) = (uint16_t)(RA8P1_PRCR_KEY |
                                          (unlock ? (RA8P1_PRCR_PRC0 |
                                                     RA8P1_PRCR_PRC1) : 0U));
}

/**
 * @brief Bring up the graphics power domain, which is gated out of reset.
 *
 * @return TIKU_GLCDC_OK, or TIKU_GLCDC_ERR_TIMEOUT if it never ungates
 */
static int
glcdc_power_on(void)
{
    uint32_t spins;

    glcdc_protect(1);
    TIKU_REG8(RA8P1_PDCTRGD) = 0U;
    glcdc_protect(0);

    for (spins = 0U; spins < GLCDC_WAIT_SPINS; spins++) {
        uint8_t pd = TIKU_REG8(RA8P1_PDCTRGD);

        if ((pd & (RA8P1_PDCTRGD_PDCSF | RA8P1_PDCTRGD_PDPGSF)) == 0U) {
            return TIKU_GLCDC_OK;
        }
    }
    return TIKU_GLCDC_ERR_TIMEOUT;
}

/**
 * @brief Point LCDCLK at MOCO undivided, through the documented handshake.
 *
 * @note The clock stops while the request is asserted, so the write order is
 *       fixed: request, wait for ready, set source and divider, release.
 *
 * @return TIKU_GLCDC_OK, or TIKU_GLCDC_ERR_TIMEOUT if ready never comes
 */
static int
glcdc_clock_moco(void)
{
    uint32_t spins;

    glcdc_protect(1);
    TIKU_REG8(RA8P1_LCDCKCR) |= (uint8_t)RA8P1_LCDCKCR_SREQ;
    for (spins = 0U; spins < GLCDC_WAIT_SPINS; spins++) {
        if ((TIKU_REG8(RA8P1_LCDCKCR) & RA8P1_LCDCKCR_SRDY) != 0U) {
            break;
        }
    }
    if (spins == GLCDC_WAIT_SPINS) {
        glcdc_protect(0);
        return TIKU_GLCDC_ERR_TIMEOUT;
    }

    TIKU_REG8(RA8P1_LCDCKDIVCR) = 0U;                   /* 1/1 */
    TIKU_REG8(RA8P1_LCDCKCR) = (uint8_t)(RA8P1_LCDCKCR_SREQ |
                                         RA8P1_LCDCKCR_SEL_MOCO);
    TIKU_REG8(RA8P1_LCDCKCR) = (uint8_t)RA8P1_LCDCKCR_SEL_MOCO;
    glcdc_protect(0);
    return TIKU_GLCDC_OK;
}

uint32_t
tiku_glcdc_arch_pixel_hz(void)
{
    return GLCDC_PIXEL_HZ;
}

int
tiku_glcdc_arch_running(void)
{
    return (TIKU_REG32(RA8P1_GLCDC_BG_MON) & RA8P1_GLCDC_BG_MON_EN) ? 1 : 0;
}

int
tiku_glcdc_arch_vpos_take(void)
{
    if ((TIKU_REG32(RA8P1_GLCDC_SYS_STMON) & RA8P1_GLCDC_SYS_VPOS) == 0U) {
        return 0;
    }
    TIKU_REG32(RA8P1_GLCDC_SYS_STCLR) = RA8P1_GLCDC_SYS_VPOS;
    return 1;
}

int
tiku_glcdc_arch_underflow(void)
{
    return (TIKU_REG32(RA8P1_GLCDC_SYS_STMON) & RA8P1_GLCDC_SYS_L1UNDF) ? 1 : 0;
}

int
tiku_glcdc_arch_start(const tiku_glcdc_mode_t *mode, const void *fb)
{
    int rc;

    if (mode == 0) {
        return TIKU_GLCDC_ERR_INVALID;
    }
    if (mode->h_total > GLCDC_FIELD_MAX || mode->v_total > GLCDC_FIELD_MAX ||
        mode->h_active == 0U || mode->v_active == 0U ||
        mode->h_start + mode->h_active > mode->h_total ||
        mode->v_start + mode->v_active > mode->v_total) {
        return TIKU_GLCDC_ERR_INVALID;
    }
    if (glcdc_running) {
        return TIKU_GLCDC_ERR_STATE;
    }

    rc = glcdc_power_on();
    if (rc != TIKU_GLCDC_OK) {
        return rc;
    }

    TIKU_REG32(RA8P1_MSTPCRC) &= ~RA8P1_MSTPCRC_GLCDC;
    (void)TIKU_REG32(RA8P1_MSTPCRC);
    tiku_cpu_ra8p1_delay_us(30U);

    rc = glcdc_clock_moco();
    if (rc != TIKU_GLCDC_OK) {
        return rc;
    }

    /* SWRST reads 0 out of reset and 0 MEANS held in reset, so the module
     * accepts every configuration write below while doing nothing with any
     * of it.  Release it before configuring, not after. */
    TIKU_REG32(RA8P1_GLCDC_BG_EN) = RA8P1_GLCDC_BG_EN_SWRST;
    tiku_cpu_ra8p1_delay_us(10U);

    /* Background plane: total period, sync widths, then where the visible
     * window sits inside it. */
    TIKU_REG32(RA8P1_GLCDC_BG_PERI)  = (uint32_t)mode->h_total |
                                       ((uint32_t)mode->v_total << 16);
    TIKU_REG32(RA8P1_GLCDC_BG_SYNC)  = (uint32_t)(mode->h_sync - 1U) |
                                       ((uint32_t)(mode->v_sync - 1U) << 16);
    TIKU_REG32(RA8P1_GLCDC_BG_HSIZE) = (uint32_t)mode->h_active |
                                       ((uint32_t)mode->h_start << 16);
    TIKU_REG32(RA8P1_GLCDC_BG_VSIZE) = (uint32_t)mode->v_active |
                                       ((uint32_t)mode->v_start << 16);
    TIKU_REG32(RA8P1_GLCDC_BG_BGC)   = 0U;

    if (fb != 0) {
        uint32_t bytes = (uint32_t)mode->h_active * 2U;

        TIKU_REG32(RA8P1_GLCDC_GR_FLM2(1)) = (uint32_t)(uintptr_t)fb;
        TIKU_REG32(RA8P1_GLCDC_GR_FLM3(1)) = bytes << 16;
        TIKU_REG32(RA8P1_GLCDC_GR_FLM5(1)) = (bytes / 8U) |
                                             ((uint32_t)mode->v_active << 16);
        TIKU_REG32(RA8P1_GLCDC_GR_FLM6(1)) = RA8P1_GLCDC_GR_FLM6_RGB565;
        TIKU_REG32(RA8P1_GLCDC_GR_AB1(1))  = RA8P1_GLCDC_GR_AB1_DISPSEL_FB;
        TIKU_REG32(RA8P1_GLCDC_GR_FLMRD(1)) = RA8P1_GLCDC_GR_FLMRD_RENB;
        TIKU_REG32(RA8P1_GLCDC_GR_VEN(1))  = RA8P1_GLCDC_GR_VEN_PVEN;
    }

    /* Arm the detectors before starting, or the flags never set. */
    TIKU_REG32(RA8P1_GLCDC_SYS_DTCTEN) = RA8P1_GLCDC_SYS_VPOS |
                                         RA8P1_GLCDC_SYS_L1UNDF;
    TIKU_REG32(RA8P1_GLCDC_SYS_STCLR)  = RA8P1_GLCDC_SYS_VPOS |
                                         RA8P1_GLCDC_SYS_L1UNDF;
    /* Panel clock: the divider and the source may only move while the
     * output is disabled, so this is three writes rather than one. */
    TIKU_REG32(RA8P1_GLCDC_SYS_PANELCLK) = 0U;
    TIKU_REG32(RA8P1_GLCDC_SYS_PANELCLK) =
        RA8P1_GLCDC_PANELCLK_DCDR(GLCDC_PANEL_DCDR) |
        RA8P1_GLCDC_PANELCLK_LCDCLK;
    TIKU_REG32(RA8P1_GLCDC_SYS_PANELCLK) =
        RA8P1_GLCDC_PANELCLK_DCDR(GLCDC_PANEL_DCDR) |
        RA8P1_GLCDC_PANELCLK_LCDCLK | RA8P1_GLCDC_PANELCLK_EN;

    /* Everything above is staged; VEN commits it and clears itself.  SWRST
     * has to stay high in the same word or the module drops back into
     * reset as the write lands. */
    TIKU_REG32(RA8P1_GLCDC_BG_EN) = RA8P1_GLCDC_BG_EN_SWRST |
                                    RA8P1_GLCDC_BG_EN_EN |
                                    RA8P1_GLCDC_BG_EN_VEN;
    glcdc_running = 1U;
    return TIKU_GLCDC_OK;
}

void
tiku_glcdc_arch_stop(void)
{
    if (!glcdc_running) {
        return;
    }
    TIKU_REG32(RA8P1_GLCDC_GR_FLMRD(1)) = 0U;
    TIKU_REG32(RA8P1_GLCDC_BG_EN) = RA8P1_GLCDC_BG_EN_SWRST |
                                    RA8P1_GLCDC_BG_EN_VEN;
    glcdc_running = 0U;
}
