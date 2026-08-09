/*
 * Tiku Operating System v0.06
 * Simple. Ubiquitous. Intelligence, Everywhere.
 * http://tiku-os.org
 *
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 *
 * tiku_vin_arch.c - RA8P1 MIPI CSI-2 capture into memory.
 *
 * D-PHY as a receiver clocked by the sensor, the CSI-2 unpacker behind it,
 * and the VIN converting YCbCr422 to RGB565 frames in memory; the colour
 * matrix stays at the part's BT.601 reset values, which the sensor expects.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include "tiku_vin_arch.h"

#include "tiku_ra8p1_regs.h"
#include "tiku_cpu_common.h"
#include "tiku_cpu_freq_boot_arch.h"

/** @brief Bounded spin for every readiness wait. */
#define VIN_WAIT_SPINS      1000000UL

/** @brief Frame wait: QVGA at a few frames per second worst case. */
#define VIN_FRAME_SPINS     5000000UL

static uint8_t vin_up;

/**
 * @brief One D-PHY timing count: cycles of PCLKA covering @p ns plus @p ui
 *        lane unit intervals.
 *
 * The registers program durations as (count + 1) PCLKA periods, so the count
 * rounds the target up and then drops one.
 */
static uint32_t
vin_tim(uint32_t ns, uint32_t ui, uint32_t ui_ps, uint32_t pclka_hz)
{
    uint64_t ps = (uint64_t)ns * 1000ULL + (uint64_t)ui * ui_ps;
    uint64_t period_ps = 1000000000000ULL / pclka_hz;
    uint64_t count = (ps + period_ps - 1U) / period_ps;

    return (count > 0U) ? (uint32_t)(count - 1U) : 0U;
}

int
tiku_vin_arch_init(void *fb, uint16_t w, uint16_t h, uint32_t ui_ps)
{
    uint32_t pclka = (uint32_t)tiku_cpu_ra8p1_pclka_get_hz();
    uint32_t spins;

    if (fb == 0 || w == 0U || h == 0U ||
        ((uint32_t)(uintptr_t)fb & 0x3FU) != 0U || (w & 0xFU) != 0U) {
        return TIKU_VIN_ERR_INVALID;
    }

    /* Same double gate as the 2D engine: the graphics power domain first --
     * with it down every register here reads zero without faulting -- and
     * only then the module stop.  PDCTRGD is written whole; its status bits
     * refuse a fed-back write. */
    TIKU_REG16(RA8P1_PRCR_S) = (uint16_t)(RA8P1_PRCR_KEY | RA8P1_PRCR_PRC1);
    TIKU_REG8(RA8P1_PDCTRGD) = 0U;
    TIKU_REG16(RA8P1_PRCR_S) = (uint16_t)RA8P1_PRCR_KEY;
    for (spins = 0U; spins < VIN_WAIT_SPINS; spins++) {
        uint8_t pd = TIKU_REG8(RA8P1_PDCTRGD);

        if ((pd & (RA8P1_PDCTRGD_PDCSF | RA8P1_PDCTRGD_PDPGSF)) == 0U) {
            break;
        }
    }
    if (spins == VIN_WAIT_SPINS) {
        return TIKU_VIN_ERR_STATE;
    }
    TIKU_REG32(RA8P1_MSTPCRC) &= ~RA8P1_MSTPCRC_MIPI_CSI;
    (void)TIKU_REG32(RA8P1_MSTPCRC);
    tiku_cpu_ra8p1_delay_us(30U);

    /*
     * D-PHY as a receiver: the sensor drives the lanes, so no PLL here --
     * only the reference frequency, the analog power, and the timing windows
     * within which HS transitions are recognised.  The counts are PCLKA
     * cycles; the ns-plus-UI targets are the vendor's for this sensor.
     */
    TIKU_REG32(RA8P1_DPHY_MDC)   = 0U;                       /* receiver    */
    TIKU_REG32(RA8P1_DPHY_REFCR) = (pclka / 1000000UL) - 1U;
    TIKU_REG32(RA8P1_DPHY_PWRCR) = RA8P1_DPHY_PWRCR_PWRSEN;
    for (spins = 0U; spins < VIN_WAIT_SPINS; spins++) {
        if ((TIKU_REG32(RA8P1_DPHY_SFR) & RA8P1_DPHY_SFR_PWRSF) != 0U) {
            break;
        }
    }
    if (spins == VIN_WAIT_SPINS) {
        return TIKU_VIN_ERR_STATE;
    }
    TIKU_REG32(RA8P1_DPHY_TIM1) = vin_tim(600000U, 0U, ui_ps, pclka);
    TIKU_REG32(RA8P1_DPHY_TIM2) = vin_tim(75U, 0U, ui_ps, pclka) |
                                  (vin_tim(500U, 0U, ui_ps, pclka) << 8) |
                                  (vin_tim(300U, 0U, ui_ps, pclka) << 16);
    TIKU_REG32(RA8P1_DPHY_TIM3) = vin_tim(40U, 5U, ui_ps, pclka) |
                                  (vin_tim(200U, 0U, ui_ps, pclka) << 8);
    TIKU_REG32(RA8P1_DPHY_TIM4) = vin_tim(300U, 0U, ui_ps, pclka) |
                                  (vin_tim(0U, 8U, ui_ps, pclka) << 8) |
                                  (vin_tim(60U, 52U, ui_ps, pclka) << 16) |
                                  (vin_tim(60U, 0U, ui_ps, pclka) << 24);
    TIKU_REG32(RA8P1_DPHY_TIM5) = vin_tim(140U, 10U, ui_ps, pclka) |
                                  (vin_tim(60U, 4U, ui_ps, pclka) << 8) |
                                  (vin_tim(60U, 0U, ui_ps, pclka) << 16);
    TIKU_REG32(RA8P1_DPHY_TIM6) = vin_tim(60U, 0U, ui_ps, pclka);
    TIKU_REG32(RA8P1_DPHY_OCR)  = RA8P1_DPHY_OCR_DPHYEN;

    /*
     * CSI receiver: reception held off, then the lane count and packet
     * handling.  Every data type up to 0x1F is let through -- the sensor's
     * YUV422 (0x1E) and the frame/line short packets all live there.
     */
    TIKU_REG32(RA8P1_CSI_MCT3) = 0U;
    for (spins = 0U; spins < VIN_WAIT_SPINS; spins++) {
        if ((TIKU_REG32(RA8P1_CSI_RTST) & RA8P1_CSI_RTST_VSRSTS) == 0U) {
            break;
        }
    }
    if (spins == VIN_WAIT_SPINS) {
        return TIKU_VIN_ERR_STATE;
    }
    TIKU_REG32(RA8P1_CSI_MCT0) = RA8P1_CSI_MCT0_2LANE;
    /*
     * Packet-end detection and lane-deskew run on ratios of the video clock
     * to the lanes' byte clock; both fields are REQUIRED computed values
     * (UM 67.3.3), and zero here mangles every packet into a malformed runt.
     */
    {
        uint32_t hsclk = (uint32_t)(1000000000000ULL / (8ULL * ui_ps));
        uint32_t frrclk = (3U * (pclka / 1000U)) / (2U * (hsclk / 1000U)) + 1U;
        uint32_t frrskw = (3U * (pclka / 1000U)) / (hsclk / 1000U) + 1U;

        TIKU_REG32(RA8P1_CSI_MCT2) = (frrskw << 16) | frrclk;
    }
    TIKU_REG32(RA8P1_CSI_EPCT) = 0U;
    TIKU_REG32(RA8P1_CSI_EMCT) = 0U;
    /* Only the sensor's video data type feeds the pixel interface; the sync
     * short packets are handled by the virtual-channel logic regardless. */
    TIKU_REG32(RA8P1_CSI_DTEL) = (1UL << 0x1E);
    TIKU_REG32(RA8P1_CSI_DTEH) = 0U;
    TIKU_REG32(RA8P1_CSI_GSCT) = 0U;

    /*
     * VIN: geometry first, colour conversion left enabled so YCbCr422 goes
     * to memory as RGB565.  Preclip coordinates are one-based and inclusive.
     * All three buffer slots point at the same frame: the demo reads a still,
     * and a ring would only matter once someone consumes frames continuously.
     */
    TIKU_REG32(RA8P1_VIN_FC)       = 0U;
    TIKU_REG32(RA8P1_VIN_MC)       = RA8P1_VIN_MC_CFG;
    TIKU_REG32(RA8P1_VIN_CSI_IFMD) = RA8P1_VIN_IFMD_CFG;
    TIKU_REG32(RA8P1_VIN_CSIFLD)   = 1U;
    TIKU_REG32(RA8P1_VIN_SLPRC)    = 1U;
    TIKU_REG32(RA8P1_VIN_ELPRC)    = h;
    TIKU_REG32(RA8P1_VIN_SPPRC)    = 1U;
    TIKU_REG32(RA8P1_VIN_EPPRC)    = w;
    TIKU_REG32(RA8P1_VIN_IS)       = w;
    TIKU_REG32(RA8P1_VIN_MB1)      = (uint32_t)(uintptr_t)fb;
    TIKU_REG32(RA8P1_VIN_MB2)      = (uint32_t)(uintptr_t)fb;
    TIKU_REG32(RA8P1_VIN_MB3)      = (uint32_t)(uintptr_t)fb;
    TIKU_REG32(RA8P1_VIN_UVAOF)    = 0U;
    TIKU_REG32(RA8P1_VIN_DMR)      = RA8P1_VIN_DMR_CFG;
    TIKU_REG32(RA8P1_VIN_UDS_CTRL) = 0U;
    TIKU_REG32(RA8P1_VIN_IE)       = 0U;      /* polled */
    TIKU_REG32(RA8P1_VIN_INTS)     = 0xFFFFFFFFUL;

    vin_up = 1U;
    return TIKU_VIN_OK;
}

int
tiku_vin_arch_start(void)
{
    uint32_t i;

    if (!vin_up) {
        return TIKU_VIN_ERR_STATE;
    }
    /* Initialise the unit's internal state, give it its settling reads, then
     * enable and enter continuous capture; the receiver is opened last so
     * frames only flow once there is somewhere for them to land. */
    TIKU_REG32(RA8P1_VIN_MC) |= RA8P1_VIN_MC_ST;
    for (i = 0U; i < 10U; i++) {
        (void)TIKU_REG32(RA8P1_VIN_MC);
    }
    TIKU_REG32(RA8P1_VIN_MC) |= RA8P1_VIN_MC_ME;
    TIKU_REG32(RA8P1_VIN_FC) |= RA8P1_VIN_FC_CC;
    TIKU_REG32(RA8P1_CSI_MCT3) = RA8P1_CSI_MCT3_RXEN;
    return TIKU_VIN_OK;
}

int
tiku_vin_arch_frame_wait(void)
{
    uint32_t spins;

    if (!vin_up) {
        return TIKU_VIN_ERR_STATE;
    }
    TIKU_REG32(RA8P1_VIN_INTS) = RA8P1_VIN_INTS_FIS;
    for (spins = 0U; spins < VIN_FRAME_SPINS; spins++) {
        if ((TIKU_REG32(RA8P1_VIN_INTS) & RA8P1_VIN_INTS_FIS) != 0U) {
            TIKU_REG32(RA8P1_VIN_INTS) = RA8P1_VIN_INTS_FIS;
            return TIKU_VIN_OK;
        }
    }
    return TIKU_VIN_ERR_TIMEOUT;
}

uint32_t
tiku_vin_arch_status(void)
{
    return TIKU_REG32(RA8P1_VIN_MS);
}

uint32_t
tiku_vin_arch_ints(void)
{
    return TIKU_REG32(RA8P1_VIN_INTS);
}
