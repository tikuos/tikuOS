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
#include "tiku_gpio_arch.h"

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

/* Which LCDCLK source the next start() uses.  MOCO needs no PLL and suits the
 * timing tests; a panel needs the real pixel rate, which only a PLL gives. */
#define GLCDC_SRC_MOCO   0U
#define GLCDC_SRC_PLL1P  1U
static uint8_t glcdc_pixel_source;

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

    if (glcdc_pixel_source == GLCDC_SRC_PLL1P) {
        /* PLL1P follows the core rung, so this is only the panel rate at
         * the 240 MHz rung: 240/4 = 60 MHz, and DCDR passes it through. */
        TIKU_REG8(RA8P1_LCDCKDIVCR) = (uint8_t)RA8P1_LCDCKDIV_4;
        TIKU_REG8(RA8P1_LCDCKCR) = (uint8_t)(RA8P1_LCDCKCR_SREQ |
                                             RA8P1_LCDCKCR_SEL_PLL1P);
        TIKU_REG8(RA8P1_LCDCKCR) = (uint8_t)RA8P1_LCDCKCR_SEL_PLL1P;
    } else {
        TIKU_REG8(RA8P1_LCDCKDIVCR) = 0U;               /* 1/1 */
        TIKU_REG8(RA8P1_LCDCKCR) = (uint8_t)(RA8P1_LCDCKCR_SREQ |
                                             RA8P1_LCDCKCR_SEL_MOCO);
        TIKU_REG8(RA8P1_LCDCKCR) = (uint8_t)RA8P1_LCDCKCR_SEL_MOCO;
    }
    glcdc_protect(0);
    return TIKU_GLCDC_OK;
}

/*
 * The parallel graphics board's 24 data lines, then LCD_CLK and the four TCON
 * signals, as port<<8 | pin.  Board manual table 33; the data order is not
 * monotonic in the pin numbers and copying it by eye is how a display comes up
 * with its colour channels swapped.
 */
static const uint16_t glcdc_panel_pins[] = {
    0x090E, 0x090F, 0x0903, 0x0902, 0x090A, 0x090B, 0x090C, 0x090D, /* D0-7  */
    0x0904, 0x0207, 0x0B07, 0x0B06, 0x0B05, 0x0B01, 0x0B04, 0x0B03, /* D8-15 */
    0x0B02, 0x0B00, 0x0707, 0x070B, 0x070C, 0x070D, 0x070E, 0x070F, /* D16-23*/
    0x050F,                                                         /* CLK   */
    0x0806, 0x0805, 0x0807, 0x050D,                                 /* TCON  */
};
#define GLCDC_PANEL_NPINS (sizeof glcdc_panel_pins / sizeof glcdc_panel_pins[0])

/** @brief Backlight enable and panel reset, driven as plain outputs. */
#define GLCDC_PANEL_BLEN_PORT   5U
#define GLCDC_PANEL_BLEN_PIN    14U
#define GLCDC_PANEL_RST_PORT    6U
#define GLCDC_PANEL_RST_PIN     6U

/*
 * Route one pin to the display controller at high drive.  Not
 * tiku_ra8p1_gpio_init_peripheral(): that leaves the drive strength at its
 * default, and an under-driven 60 MHz bus of 29 lines gives pixels that are
 * almost right rather than none at all -- the same reason the OSPI pins ask
 * for it explicitly.
 */
static void
glcdc_pin_to_glcdc(uint32_t port, uint32_t pin)
{
    TIKU_REG32(RA8P1_PFS(port, pin)) =
        (RA8P1_PFS_PSEL_GLCDC << RA8P1_PFS_PSEL_SHIFT) |
        RA8P1_PFS_PMR | RA8P1_PFS_DSCR_HS_HIGH;
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
        /* Both FLM5 counts are minus-one: DATANUM in 64-byte bursts per
         * line, LNNUM in lines.  The demo's live values are the reference
         * (23 bursts for 1536 bytes, 449 for a 450-line window). */
        TIKU_REG32(RA8P1_GLCDC_GR_FLM5(1)) = ((bytes / 64U) - 1U) |
                                     (((uint32_t)mode->v_active - 1U) << 16);
        /* FLM1 as the demo runs it; the field is undocumented in the UM's
         * text but the working configuration sets 3. */
        TIKU_REG32(RA8P1_GLCDC_GR_FLM1(1)) = 3U;
        TIKU_REG32(RA8P1_GLCDC_GR_FLM6(1)) = RA8P1_GLCDC_GR_FLM6_RGB565;
        /* Where the layer lands, in the background plane's own coordinates:
         * without this the window is whatever reset left behind. */
        TIKU_REG32(RA8P1_GLCDC_GR_AB2(1))  = (uint32_t)mode->v_active |
            ((uint32_t)mode->v_start << 16);
        TIKU_REG32(RA8P1_GLCDC_GR_AB3(1))  = (uint32_t)mode->h_active |
            ((uint32_t)mode->h_start << 16);
        TIKU_REG32(RA8P1_GLCDC_GR_AB1(1))  = RA8P1_GLCDC_GR_AB1_DISPSEL_FB;
        TIKU_REG32(RA8P1_GLCDC_GR_FLMRD(1)) = RA8P1_GLCDC_GR_FLMRD_RENB;
        TIKU_REG32(RA8P1_GLCDC_GR_VEN(1))  = RA8P1_GLCDC_GR_VEN_PVEN;
    }

    /* Layer 2 sits above layer 1 and its reset state paints black over the
     * whole frame; every lower stage can be perfect and the glass stays
     * dark.  Pass-through unless a caller configures it for real. */
    TIKU_REG32(RA8P1_GLCDC_GR_AB1(2)) = RA8P1_GLCDC_GR_AB1_DISPSEL_PASS;
    TIKU_REG32(RA8P1_GLCDC_GR_VEN(2)) = RA8P1_GLCDC_GR_VEN_PVEN;

    /*
     * Timing controller.  The A pair emits the sync pulses from the start of
     * the period; the B pair emits data-enable across the visible window,
     * which begins one cycle before the background plane's own start.  The
     * SEL codes route each to the TCON pin this board wires to the panel.
     */
    TIKU_REG32(RA8P1_GLCDC_TCON_TIM)   = 0U;
    TIKU_REG32(RA8P1_GLCDC_TCON_STVA1) = (uint32_t)mode->v_sync;
    TIKU_REG32(RA8P1_GLCDC_TCON_STVA2) = 0x10U;      /* VSOUT, inverted   */
    TIKU_REG32(RA8P1_GLCDC_TCON_STVB1) = (uint32_t)mode->v_active |
        ((uint32_t)(mode->v_start - 1U) << 16);
    TIKU_REG32(RA8P1_GLCDC_TCON_STVB2) = 0x02U;      /* VEOUT = DE        */
    TIKU_REG32(RA8P1_GLCDC_TCON_STHA1) = (uint32_t)mode->h_sync;
    TIKU_REG32(RA8P1_GLCDC_TCON_STHA2) = 0x17U;      /* HSOUT, inverted   */
    TIKU_REG32(RA8P1_GLCDC_TCON_STHB1) = (uint32_t)mode->h_active |
        ((uint32_t)(mode->h_start - 1U) << 16);
    TIKU_REG32(RA8P1_GLCDC_TCON_STHB2) = 0U;
    TIKU_REG32(RA8P1_GLCDC_TCON_DE)    = 0U;
    TIKU_REG32(RA8P1_GLCDC_OUT_SET)    = 0U;         /* 24-bit parallel   */

    /* Output correction multiplies, and zero is its reset value. */
    TIKU_REG32(RA8P1_GLCDC_OUT_BRIGHT1) = RA8P1_GLCDC_BRIGHT_MID;
    TIKU_REG32(RA8P1_GLCDC_OUT_BRIGHT2) = (RA8P1_GLCDC_BRIGHT_MID << 16) |
                                          RA8P1_GLCDC_BRIGHT_MID;
    TIKU_REG32(RA8P1_GLCDC_OUT_CONTRAST) = RA8P1_GLCDC_CONTRAST_UNITY;
    /* The output block has its OWN reflect bit, separate from BG_EN.VEN and
     * the per-layer PVENs.  Without this pulse none of the OUT_* writes --
     * the contrast among them -- ever reach the hardware, and the output
     * stage keeps multiplying every pixel by its reset value of zero. */
    TIKU_REG32(RA8P1_GLCDC_OUT_VLATCH) = 1U;

    /* Arm the detectors before starting, or the flags never set. */
    TIKU_REG32(RA8P1_GLCDC_SYS_DTCTEN) = RA8P1_GLCDC_SYS_VPOS |
                                         RA8P1_GLCDC_SYS_L1UNDF;
    TIKU_REG32(RA8P1_GLCDC_SYS_STCLR)  = RA8P1_GLCDC_SYS_VPOS |
                                         RA8P1_GLCDC_SYS_L1UNDF;
    /* Panel clock: the divider and the source may only move while the
     * output is disabled, so this is three writes rather than one. */
    TIKU_REG32(RA8P1_GLCDC_SYS_PANELCLK) = 0U;
    TIKU_REG32(RA8P1_GLCDC_SYS_PANELCLK) =
        RA8P1_GLCDC_PANELCLK_DCDR(glcdc_pixel_source == GLCDC_SRC_PLL1P ? 1U : GLCDC_PANEL_DCDR) |
        RA8P1_GLCDC_PANELCLK_LCDCLK;
    TIKU_REG32(RA8P1_GLCDC_SYS_PANELCLK) =
        RA8P1_GLCDC_PANELCLK_DCDR(glcdc_pixel_source == GLCDC_SRC_PLL1P ? 1U : GLCDC_PANEL_DCDR) |
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

int
tiku_glcdc_arch_rebind(const void *fb)
{
    if (!glcdc_running || fb == 0) {
        return TIKU_GLCDC_ERR_STATE;
    }
    TIKU_REG32(RA8P1_GLCDC_GR_FLM2(1)) = (uint32_t)(uintptr_t)fb;
    TIKU_REG32(RA8P1_GLCDC_GR_VEN(1))  = RA8P1_GLCDC_GR_VEN_PVEN;
    return TIKU_GLCDC_OK;
}

int
tiku_glcdc_arch_panel_start(const void *fb)
{
    /* The panel's own timing; the demo runs the same numbers on this board. */
    static const tiku_glcdc_mode_t panel = {
        .h_active = TIKU_GLCDC_PANEL_W, .h_total = 1334U,
        .h_sync = 10U, .h_start = 301U,
        .v_active = TIKU_GLCDC_PANEL_H, .v_total = 780U,
        .v_sync = 2U, .v_start = 31U,
    };
    uint32_t i;
    int rc;

    if (fb == 0) {
        return TIKU_GLCDC_ERR_INVALID;
    }
    /* Already scanning: rebinding costs a register write and a frame, where
     * a restart would blank the panel and re-run the reset dance. */
    if (glcdc_running) {
        return tiku_glcdc_arch_rebind(fb);
    }

    /* Panel reset low, backlight off, while the pins are still settling. */
    tiku_ra8p1_gpio_init_output(GLCDC_PANEL_RST_PORT, GLCDC_PANEL_RST_PIN);
    tiku_ra8p1_gpio_set(GLCDC_PANEL_RST_PORT, GLCDC_PANEL_RST_PIN, 0);
    tiku_ra8p1_gpio_init_output(GLCDC_PANEL_BLEN_PORT, GLCDC_PANEL_BLEN_PIN);
    tiku_ra8p1_gpio_set(GLCDC_PANEL_BLEN_PORT, GLCDC_PANEL_BLEN_PIN, 0);

    TIKU_REG8(RA8P1_PWPR_S) = 0U;
    TIKU_REG8(RA8P1_PWPR_S) = (uint8_t)RA8P1_PWPR_PFSWE;
    for (i = 0U; i < GLCDC_PANEL_NPINS; i++) {
        glcdc_pin_to_glcdc((uint32_t)(glcdc_panel_pins[i] >> 8),
                           (uint32_t)(glcdc_panel_pins[i] & 0xFFU));
    }
    TIKU_REG8(RA8P1_PWPR_S) = (uint8_t)RA8P1_PWPR_B0WI;
    __asm__ volatile ("dsb" ::: "memory");

    glcdc_pixel_source = GLCDC_SRC_PLL1P;
    rc = tiku_glcdc_arch_start(&panel, fb);
    glcdc_pixel_source = GLCDC_SRC_MOCO;
    if (rc != TIKU_GLCDC_OK) {
        return rc;
    }

    /* Release reset once pixels are already flowing, then light it. */
    tiku_cpu_ra8p1_delay_ms(20U);
    tiku_ra8p1_gpio_set(GLCDC_PANEL_RST_PORT, GLCDC_PANEL_RST_PIN, 1);
    tiku_cpu_ra8p1_delay_ms(120U);
    tiku_ra8p1_gpio_set(GLCDC_PANEL_BLEN_PORT, GLCDC_PANEL_BLEN_PIN, 1);
    return TIKU_GLCDC_OK;
}
