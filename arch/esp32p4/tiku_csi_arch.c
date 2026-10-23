/*
 * Tiku Operating System v0.06
 * Simple. Persistent. Intelligent.
 * http://tiku-os.org
 *
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 *
 * tiku_csi_arch.c - P4 MIPI CSI-2 receiver: D-PHY supply and clocks, the
 * band of the lane rate through the PHY's test interface, the host's lanes.
 * Sequence and band table derived from ESP-IDF 383dda6c mipi_csi_hal.c,
 * mipi_csi_periph.c and esp_cam_ctlr_csi.c; registers from hw_ver1.
 *
 * SPDX-FileCopyrightText: 2023-2026 Espressif Systems (Shanghai) CO LTD
 * SPDX-License-Identifier: Apache-2.0
 */
#include <stdint.h>
#include "tiku_csi_arch.h"
#include "tiku_pmu_arch.h"
#include "tiku_esp32p4_regs.h"
#include "tiku_cpu_common.h"

/* HP_SYS_CLKRST: the host's bus clock and reset, the D-PHY's config clock
 * (PLL_F20M, source code 0). */
#define SOC_CLK_CTRL1   0x500E6018u
#define BRG_SYS_CLK_EN  (1u << 10)
#define HOST_SYS_CLK_EN (1u << 11)
#define PERI_CLK_CTRL03 0x500E603Cu
#define DPHY_CLK_SRC    (3u << 2)
#define DPHY_CFG_CLK_EN (1u << 4)
#define RST_EN0         0x500E60C0u
#define RST_HOST        (1u << 27)
#define RST_BRG         (1u << 28)

/* The bridge's HOST_CTRL: csi_enableclk clocks the host; without it a
 * host register access is a bus error. */
#define BRG_HOST_CTRL   0x5009F840u
#define BRG_HOST_CLK    (1u << 0)

/* The host controller. */
#define HOST_BASE       0x5009F000u
#define H_N_LANES       (HOST_BASE + 0x004u)
#define H_CSI2_RESETN   (HOST_BASE + 0x008u)
#define H_INT_ST_MAIN   (HOST_BASE + 0x00Cu)
#define H_PHY_SHUTDOWNZ (HOST_BASE + 0x040u)
#define H_DPHY_RSTZ     (HOST_BASE + 0x044u)
#define H_PHY_STOPSTATE (HOST_BASE + 0x04Cu)
#define H_PHY_TEST0     (HOST_BASE + 0x050u)   /* testclr bit 0, testclk bit 1 */
#define H_PHY_TEST1     (HOST_BASE + 0x054u)   /* testdin [7:0], testen bit 16 */
#define H_VC_EXTENSION  (HOST_BASE + 0x0C8u)   /* vcx: 1 turns the extension off */
#define H_ST_PHY_FATAL  (HOST_BASE + 0x0E0u)
#define H_MSK_PHY_FATAL (HOST_BASE + 0x0E4u)
#define H_ST_PKT_FATAL  (HOST_BASE + 0x0F0u)
#define H_MSK_PKT_FATAL (HOST_BASE + 0x0F4u)
#define H_ST_PHY        (HOST_BASE + 0x110u)
#define H_MSK_PHY       (HOST_BASE + 0x114u)
#define H_ST_BNDRY      (HOST_BASE + 0x280u)   /* then SEQ at +0x10, CRC at +0x20 */
#define H_ST_PLD_CRC    (HOST_BASE + 0x2B0u)
#define H_ST_DATA_ID    (HOST_BASE + 0x2C0u)
#define H_ST_ECC        (HOST_BASE + 0x2D0u)
#define H_MSK(st)       ((st) + 4u)
#define H_SCRAMBLING    (HOST_BASE + 0x300u)
#define TEST_CLK        (1u << 1)
#define TEST_CLR        (1u << 0)
#define TEST_EN         (1u << 16)
#define PHY_HSFREQ_REG  0x44u                   /* hsfreqrange in bits [6:1] */

#define MIPI_LDO_MV     2500u
#define LDO_SETTLE_US   200u

/** @brief The D-PHY's bands: the lowest and highest Mbit/s of each and its
 *         hsfreqrange code. */
static const struct {
    uint16_t lo, hi;
    uint8_t code;
} bands[] = {
    {90, 99, 0x00}, {100, 109, 0x10}, {110, 129, 0x20}, {130, 139, 0x01},
    {140, 149, 0x11}, {150, 169, 0x21}, {170, 179, 0x02}, {180, 199, 0x12},
    {200, 219, 0x22}, {220, 239, 0x03}, {240, 249, 0x13}, {250, 269, 0x23},
    {270, 299, 0x04}, {300, 329, 0x14}, {330, 359, 0x05}, {360, 399, 0x15},
    {400, 449, 0x25}, {450, 499, 0x06}, {500, 549, 0x16}, {550, 599, 0x07},
    {600, 649, 0x17}, {650, 699, 0x08}, {700, 749, 0x18}, {750, 799, 0x09},
    {800, 849, 0x19}, {850, 899, 0x29}, {900, 949, 0x39}, {950, 999, 0x0A},
    {1000, 1049, 0x1A}, {1050, 1099, 0x2A}, {1100, 1149, 0x3A},
    {1150, 1199, 0x0B}, {1200, 1249, 0x1B}, {1250, 1299, 0x2B},
    {1300, 1349, 0x3B}, {1350, 1399, 0x0C}, {1400, 1449, 0x1C},
    {1450, 1500, 0x2C},
};

static void put_bits(uint32_t reg, uint32_t bits, uint32_t value)
{
    TIKU_P4_REG_WRITE(reg, (TIKU_P4_REG_READ(reg) & ~bits) | (value & bits));
}

uint8_t tiku_p4_csi_hsfreq(uint32_t lane_mbps)
{
    uint32_t i;

    for (i = 0u; i < sizeof bands / sizeof bands[0]; i++) {
        if (lane_mbps >= bands[i].lo && lane_mbps <= bands[i].hi) {
            return bands[i].code;
        }
    }
    return 0xFFu;
}

/** @brief Write @p value to the D-PHY's register @p addr: the address
 *         clocked in with testen, then the value. */
static void phy_write(uint8_t addr, uint8_t value)
{
    TIKU_P4_REG_WRITE(H_PHY_TEST0, 0u);
    TIKU_P4_REG_WRITE(H_PHY_TEST1, TEST_EN | addr);
    TIKU_P4_REG_WRITE(H_PHY_TEST0, TEST_CLK);
    TIKU_P4_REG_WRITE(H_PHY_TEST0, 0u);
    TIKU_P4_REG_WRITE(H_PHY_TEST1, value);
    TIKU_P4_REG_WRITE(H_PHY_TEST0, TEST_CLK);
    TIKU_P4_REG_WRITE(H_PHY_TEST0, 0u);
}

TIKU_P4_XIP_TEXT
int tiku_p4_csi_start(uint8_t lanes, uint32_t lane_mbps)
{
    uint8_t code = tiku_p4_csi_hsfreq(lane_mbps);
    uint32_t st;

    if ((lanes != 1u && lanes != 2u) || code == 0xFFu) {
        return TIKU_P4_CSI_ERR_PARAM;
    }
    if (tiku_p4_pmu_mipi_ldo_init(MIPI_LDO_MV) != 0) {
        return TIKU_P4_CSI_ERR_POWER;
    }
    tiku_cpu_p4_delay_us(LDO_SETTLE_US);
    /* The bridge clocked, reset and clocking the host, as ESP-IDF's
     * mipi_csi_brg_claim() leaves it; the host's bus clock cycled and its
     * registers reset; the D-PHY's config clock from PLL_F20M. */
    put_bits(SOC_CLK_CTRL1, BRG_SYS_CLK_EN, BRG_SYS_CLK_EN);
    put_bits(RST_EN0, RST_BRG, RST_BRG);
    put_bits(RST_EN0, RST_BRG, 0u);
    TIKU_P4_REG_WRITE(BRG_HOST_CTRL, BRG_HOST_CLK);
    put_bits(SOC_CLK_CTRL1, HOST_SYS_CLK_EN, 0u);
    put_bits(SOC_CLK_CTRL1, HOST_SYS_CLK_EN, HOST_SYS_CLK_EN);
    put_bits(RST_EN0, RST_HOST, RST_HOST);
    put_bits(RST_EN0, RST_HOST, 0u);
    put_bits(PERI_CLK_CTRL03, DPHY_CLK_SRC | DPHY_CFG_CLK_EN, 0u);
    put_bits(PERI_CLK_CTRL03, DPHY_CFG_CLK_EN, DPHY_CFG_CLK_EN);
    /* PHY and host held in reset while the band is written; the test
     * interface starts from address 0 with a testclr pulse. */
    TIKU_P4_REG_WRITE(H_PHY_SHUTDOWNZ, 0u);
    TIKU_P4_REG_WRITE(H_DPHY_RSTZ, 0u);
    TIKU_P4_REG_WRITE(H_CSI2_RESETN, 0u);
    TIKU_P4_REG_WRITE(H_PHY_TEST1, TEST_EN);
    TIKU_P4_REG_WRITE(H_PHY_TEST0, TEST_CLR);
    TIKU_P4_REG_WRITE(H_PHY_TEST0, 0u);
    phy_write(PHY_HSFREQ_REG, (uint8_t)(code << 1));
    TIKU_P4_REG_WRITE(H_PHY_SHUTDOWNZ, 1u);
    TIKU_P4_REG_WRITE(H_DPHY_RSTZ, 1u);
    TIKU_P4_REG_WRITE(H_CSI2_RESETN, 1u);
    /* One virtual channel: the extension off; no scrambling. */
    TIKU_P4_REG_WRITE(H_N_LANES, lanes - 1u);
    TIKU_P4_REG_WRITE(H_VC_EXTENSION, 1u);
    TIKU_P4_REG_WRITE(H_SCRAMBLING, 0u);
    /* Every error group latched in its status register; the host's line is
     * not routed, so nothing is raised.  The reads clear what reset left. */
    TIKU_P4_REG_WRITE(H_MSK_PHY_FATAL, 0x3u);
    TIKU_P4_REG_WRITE(H_MSK_PKT_FATAL, 0x3u);
    TIKU_P4_REG_WRITE(H_MSK_PHY, 0x30003u);
    for (st = H_ST_BNDRY; st <= H_ST_ECC; st += 0x10u) {
        TIKU_P4_REG_WRITE(H_MSK(st), 0xFFFFu);
    }
    {
        struct tiku_p4_csi_status s;
        tiku_p4_csi_status(&s);
    }
    return TIKU_P4_CSI_OK;
}

TIKU_P4_XIP_TEXT
void tiku_p4_csi_stop(void)
{
    TIKU_P4_REG_WRITE(H_CSI2_RESETN, 0u);
    TIKU_P4_REG_WRITE(H_DPHY_RSTZ, 0u);
    TIKU_P4_REG_WRITE(H_PHY_SHUTDOWNZ, 0u);
    put_bits(SOC_CLK_CTRL1, HOST_SYS_CLK_EN, 0u);
}

TIKU_P4_XIP_TEXT
void tiku_p4_csi_status(struct tiku_p4_csi_status *s)
{
    s->main = TIKU_P4_REG_READ(H_INT_ST_MAIN);
    s->phy_fatal = TIKU_P4_REG_READ(H_ST_PHY_FATAL);
    s->pkt_fatal = TIKU_P4_REG_READ(H_ST_PKT_FATAL);
    s->frame_fatal = TIKU_P4_REG_READ(H_ST_BNDRY) |
                     TIKU_P4_REG_READ(H_ST_BNDRY + 0x10u) |
                     TIKU_P4_REG_READ(H_ST_BNDRY + 0x20u);
    s->pld_crc = TIKU_P4_REG_READ(H_ST_PLD_CRC);
    s->data_id = TIKU_P4_REG_READ(H_ST_DATA_ID);
    s->ecc_corrected = TIKU_P4_REG_READ(H_ST_ECC);
    s->phy = TIKU_P4_REG_READ(H_ST_PHY);
    s->stopstate = TIKU_P4_REG_READ(H_PHY_STOPSTATE);
}
