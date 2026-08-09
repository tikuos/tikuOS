/*
 * Tiku Operating System v0.06
 * Simple. Ubiquitous. Intelligence, Everywhere.
 * http://tiku-os.org
 *
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 *
 * tiku_camera_arch.c - RA8P1 camera bring-up: clock, power, sensor access.
 *
 * The OV5640 on the camera expansion board: XCLK from GPT channel 12 on
 * P501, reset on P709, the board's MIPI-path switch on P108, registers
 * over IIC1 at 0x3C.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include "tiku_camera_arch.h"

#include "tiku_ra8p1_regs.h"
#include "tiku_cpu_common.h"
#include "tiku_cpu_freq_boot_arch.h"
#include "tiku_gpio_arch.h"
#include "tiku_i2c_arch.h"

/** @brief The sensor's 7-bit bus address. */
#define CAM_I2C_ADDR        0x3CU

/** @brief XCLK the sensor's register tables assume. */
#define CAM_XCLK_HZ         24000000UL

/** @brief GPT channel whose A output is the connector's XCLK pin. */
#define CAM_GPT_CH          12U
#define CAM_XCLK_PORT       5U
#define CAM_XCLK_PIN        1U

/** @brief Reset, active low; the sensor has no MCU-driven power-down here. */
#define CAM_RST_PORT        7U
#define CAM_RST_PIN         9U

/** @brief Board MIPI-path switch enable, active low (P108). */
#define CAM_MIPI_EN_PORT    1U
#define CAM_MIPI_EN_PIN     8U

static uint8_t cam_up;

/**
 * @brief Run the sensor's external clock from the timer.
 *
 * @return TIKU_CAM_OK, or TIKU_CAM_ERR_STATE when the rate cannot be made
 */
static int
cam_xclk_start(void)
{
    unsigned long pclkd = tiku_cpu_ra8p1_pclkd_get_hz();
    uint32_t n = (uint32_t)(pclkd / CAM_XCLK_HZ);

    /* The sensor accepts 6..54 MHz but its PLL settings are computed for
     * 24 MHz; a divider that cannot reach it exactly means those tables
     * would quietly aim the MIPI link at the wrong rate. */
    if (n == 0U || (pclkd % CAM_XCLK_HZ) != 0UL) {
        return TIKU_CAM_ERR_STATE;
    }

    TIKU_REG32(RA8P1_MSTPCRE) &= ~RA8P1_MSTPCRE_GPT12;
    (void)TIKU_REG32(RA8P1_MSTPCRE);
    tiku_cpu_ra8p1_delay_us(30U);

    TIKU_REG8(RA8P1_PWPR_S) = 0U;
    TIKU_REG8(RA8P1_PWPR_S) = (uint8_t)RA8P1_PWPR_PFSWE;
    TIKU_REG32(RA8P1_PFS(CAM_XCLK_PORT, CAM_XCLK_PIN)) =
        (RA8P1_PFS_PSEL_GPT << RA8P1_PFS_PSEL_SHIFT) |
        RA8P1_PFS_PMR | RA8P1_PFS_DSCR_HS_HIGH;
    TIKU_REG8(RA8P1_PWPR_S) = (uint8_t)RA8P1_PWPR_B0WI;

    TIKU_REG32(RA8P1_GPT_GTWP(CAM_GPT_CH))   = RA8P1_GPT_GTWP_KEY;  /* unlock */
    TIKU_REG32(RA8P1_GPT_GTCR(CAM_GPT_CH))   = 0U;                /* stop   */
    TIKU_REG32(RA8P1_GPT_GTCNT(CAM_GPT_CH))  = 0U;
    TIKU_REG32(RA8P1_GPT_GTPR(CAM_GPT_CH))   = n - 1U;
    TIKU_REG32(RA8P1_GPT_GTCCRA(CAM_GPT_CH)) = n / 2U;
    TIKU_REG32(RA8P1_GPT_GTUDDTYC(CAM_GPT_CH)) = 1U;              /* up     */
    TIKU_REG32(RA8P1_GPT_GTIOR(CAM_GPT_CH))  = RA8P1_GPT_GTIOA_CLK |
                                             RA8P1_GPT_GTIOR_OAE;
    TIKU_REG32(RA8P1_GPT_GTCR(CAM_GPT_CH))   = RA8P1_GPT_GTCR_CST;  /* run    */
    return TIKU_CAM_OK;
}

int
tiku_camera_arch_power_on(void)
{
    int rc;

    if (cam_up) {
        return TIKU_CAM_OK;
    }
    rc = cam_xclk_start();
    if (rc != TIKU_CAM_OK) {
        return rc;
    }
    if (tiku_i2c_arch_init(0) != TIKU_I2C_OK) {
        return TIKU_CAM_ERR_STATE;
    }

    /* The board interposes a switch between the camera connector's MIPI
     * lanes and the D-PHY pads; P108 low is what closes it.  Left at its
     * default the lanes read dead at the receiver while the sensor streams
     * into an open circuit -- no error surfaces anywhere. */
    tiku_ra8p1_gpio_init_output(CAM_MIPI_EN_PORT, CAM_MIPI_EN_PIN);
    tiku_ra8p1_gpio_set(CAM_MIPI_EN_PORT, CAM_MIPI_EN_PIN, 0);

    /*
     * The EK-RA8P1 gives the sensor only a reset line (P709); its power-down is
     * not MCU-driven -- the pin the module datasheet calls PWDN is a PMOD GPIO
     * on this board, so driving it is wrong.  With XCLK already running, hold
     * reset asserted long enough for the analog blocks to settle, then release
     * and pulse once more, matching the vendor's sequence.  Too short a settle
     * and the sensor answers its address but clock-stretches every register
     * read while it is still coming up.
     */
    tiku_ra8p1_gpio_init_output(CAM_RST_PORT, CAM_RST_PIN);
    tiku_ra8p1_gpio_set(CAM_RST_PORT, CAM_RST_PIN, 0);
    tiku_cpu_ra8p1_delay_ms(300U);
    tiku_ra8p1_gpio_set(CAM_RST_PORT, CAM_RST_PIN, 1);
    tiku_cpu_ra8p1_delay_ms(20U);
    tiku_ra8p1_gpio_set(CAM_RST_PORT, CAM_RST_PIN, 0);
    tiku_cpu_ra8p1_delay_ms(20U);
    tiku_ra8p1_gpio_set(CAM_RST_PORT, CAM_RST_PIN, 1);
    tiku_cpu_ra8p1_delay_ms(20U);

    cam_up = 1U;
    return TIKU_CAM_OK;
}

int
tiku_camera_arch_read_reg(uint16_t reg, uint8_t *val)
{
    uint8_t a[2];
    unsigned attempt;
    int rc = TIKU_I2C_ERR_TIMEOUT;

    if (val == 0) {
        return TIKU_CAM_ERR_BUS;
    }
    /* Repeated start, no stop between: the OV5640 read protocol keeps the bus
     * from the register-pointer write through the data read, and a stop in
     * the middle ends the transaction before the byte is clocked.  Same
     * bounded retry as the write path, for the same state-change stalls. */
    a[0] = (uint8_t)(reg >> 8);
    a[1] = (uint8_t)reg;
    for (attempt = 0U; attempt < 2U; attempt++) {
        rc = tiku_i2c_arch_write_read(CAM_I2C_ADDR, a, 2U, val, 1U);
        if (rc == TIKU_I2C_OK) {
            return TIKU_CAM_OK;
        }
        tiku_i2c_arch_close();
        (void)tiku_i2c_arch_init(0);
        tiku_cpu_ra8p1_delay_ms(2U);
    }
    return (rc == TIKU_I2C_ERR_NACK) ? TIKU_CAM_ERR_ABSENT : TIKU_CAM_ERR_BUS;
}

int
tiku_camera_arch_write_reg(uint16_t reg, uint8_t val)
{
    uint8_t a[3];
    unsigned attempt;
    int rc = TIKU_I2C_ERR_TIMEOUT;

    a[0] = (uint8_t)(reg >> 8);
    a[1] = (uint8_t)reg;
    a[2] = val;
    /*
     * One bounded retry behind a bus rebuild: writes that change the sensor's
     * own state -- resets, power-down entry -- can stall its SCCB for a
     * moment, and a polled master runs registers back-to-back far faster
     * than the vendor's interrupt-driven stack ever did.
     */
    for (attempt = 0U; attempt < 2U; attempt++) {
        rc = tiku_i2c_arch_write(CAM_I2C_ADDR, a, 3U);
        if (rc == TIKU_I2C_OK) {
            return TIKU_CAM_OK;
        }
        tiku_i2c_arch_close();
        (void)tiku_i2c_arch_init(0);
        tiku_cpu_ra8p1_delay_ms(2U);
    }
    return (rc == TIKU_I2C_ERR_NACK) ? TIKU_CAM_ERR_ABSENT : TIKU_CAM_ERR_BUS;
}

int
tiku_camera_arch_read_id(uint16_t *id)
{
    uint8_t hi, lo;
    int rc;

    if (id == 0) {
        return TIKU_CAM_ERR_BUS;
    }
    rc = tiku_camera_arch_read_reg(0x300AU, &hi);
    if (rc != TIKU_CAM_OK) {
        return rc;
    }
    rc = tiku_camera_arch_read_reg(0x300BU, &lo);
    if (rc != TIKU_CAM_OK) {
        return rc;
    }
    *id = (uint16_t)(((uint16_t)hi << 8) | lo);
    return (*id == 0x5640U) ? TIKU_CAM_OK : TIKU_CAM_ERR_ID;
}

/*---------------------------------------------------------------------------*/
/* Sensor configuration                                                      */
/*---------------------------------------------------------------------------*/

/** @brief One sensor register write in the bring-up tables. */
typedef struct {
    uint16_t reg;
    uint8_t  val;
} cam_regval_t;

/*
 * The vendor's OV5640 bring-up for QVGA over two MIPI lanes (BSD-3 reference
 * shipped with the EK-RA8P1 vision example), with its runtime clock solver
 * replaced by the values it converges to for a 24 MHz XCLK and a 185 MHz
 * target: PLL x246 / pre-div 8 -> both sensor system and MIPI clock at
 * 184.5 MHz, 369 Mbps per lane.  The sensor is in software power-down for
 * the whole list and wakes in the second table.
 */
static const cam_regval_t cam_cfg_a[] = {
    { 0x3017U, 0x00U }, { 0x3018U, 0x00U }, { 0x3034U, 0x18U },
    { 0x3037U, 0x13U }, { 0x3108U, 0x01U }, { 0x3630U, 0x36U },
    { 0x3631U, 0x0EU }, { 0x3632U, 0xE2U }, { 0x3633U, 0x12U },
    { 0x3621U, 0xE0U }, { 0x3704U, 0xA0U }, { 0x3703U, 0x5AU },
    { 0x3715U, 0x78U }, { 0x3717U, 0x01U }, { 0x370BU, 0x60U },
    { 0x3705U, 0x1AU }, { 0x3905U, 0x02U }, { 0x3906U, 0x10U },
    { 0x3901U, 0x0AU }, { 0x3731U, 0x12U }, { 0x3600U, 0x08U },
    { 0x3601U, 0x33U }, { 0x302DU, 0x60U }, { 0x3620U, 0x52U },
    { 0x371BU, 0x20U }, { 0x471CU, 0x50U }, { 0x3A13U, 0x43U },
    { 0x3A18U, 0x00U }, { 0x3A19U, 0xF8U }, { 0x3635U, 0x13U },
    { 0x3636U, 0x03U }, { 0x3634U, 0x40U }, { 0x3622U, 0x01U },
    { 0x3C01U, 0x34U }, { 0x3C04U, 0x28U }, { 0x3C05U, 0x98U },
    { 0x3C06U, 0x00U }, { 0x3C08U, 0x00U }, { 0x3C09U, 0x1CU },
    { 0x3C0AU, 0x9CU }, { 0x3C0BU, 0x40U }, { 0x3800U, 0x00U },
    { 0x3801U, 0x00U }, { 0x3802U, 0x00U }, { 0x3803U, 0x04U },
    { 0x3804U, 0x0AU }, { 0x3805U, 0x3FU }, { 0x3806U, 0x07U },
    { 0x3807U, 0x9FU }, { 0x3810U, 0x00U }, { 0x3811U, 0x10U },
    { 0x3812U, 0x00U }, { 0x3813U, 0x00U }, { 0x3708U, 0x64U },
    { 0x3A08U, 0x01U }, { 0x4001U, 0x02U }, { 0x4005U, 0x1AU },
    { 0x3000U, 0x00U }, { 0x3002U, 0x1CU }, { 0x3004U, 0xFFU },
    { 0x3006U, 0xC3U }, { 0x300EU, 0x45U }, { 0x302EU, 0x08U },
    { 0x4300U, 0x32U }, { 0x3034U, 0x18U }, { 0x501FU, 0x00U },
    { 0x4407U, 0x04U }, { 0x440EU, 0x00U }, { 0x5000U, 0xA7U },
    { 0x5180U, 0xFFU }, { 0x5181U, 0xF2U }, { 0x5182U, 0x00U },
    { 0x5183U, 0x14U }, { 0x5184U, 0x25U }, { 0x5185U, 0x24U },
    { 0x5186U, 0x09U }, { 0x5187U, 0x09U }, { 0x5188U, 0x09U },
    { 0x5189U, 0x75U }, { 0x518AU, 0x54U }, { 0x518BU, 0xE0U },
    { 0x518CU, 0xB2U }, { 0x518DU, 0x42U }, { 0x518EU, 0x3DU },
    { 0x518FU, 0x56U }, { 0x5190U, 0x46U }, { 0x5191U, 0xF8U },
    { 0x5192U, 0x04U }, { 0x5193U, 0x70U }, { 0x5194U, 0xF0U },
    { 0x5195U, 0xF0U }, { 0x5196U, 0x03U }, { 0x5197U, 0x01U },
    { 0x5198U, 0x04U }, { 0x5199U, 0x12U }, { 0x519AU, 0x04U },
    { 0x519BU, 0x00U }, { 0x519CU, 0x06U }, { 0x519DU, 0x82U },
    { 0x519EU, 0x38U }, { 0x5381U, 0x1EU }, { 0x5382U, 0x5BU },
    { 0x5383U, 0x08U }, { 0x5384U, 0x0AU }, { 0x5385U, 0x7EU },
    { 0x5386U, 0x88U }, { 0x5387U, 0x7CU }, { 0x5388U, 0x6CU },
    { 0x5389U, 0x10U }, { 0x538AU, 0x01U }, { 0x538BU, 0x98U },
    { 0x5300U, 0x08U }, { 0x5301U, 0x30U }, { 0x5302U, 0x10U },
    { 0x5303U, 0x00U }, { 0x5304U, 0x08U }, { 0x5305U, 0x30U },
    { 0x5306U, 0x08U }, { 0x5307U, 0x16U }, { 0x5309U, 0x08U },
    { 0x530AU, 0x30U }, { 0x530BU, 0x04U }, { 0x530CU, 0x06U },
    { 0x5480U, 0x01U }, { 0x5481U, 0x08U }, { 0x5482U, 0x14U },
    { 0x5483U, 0x28U }, { 0x5484U, 0x51U }, { 0x5485U, 0x65U },
    { 0x5486U, 0x71U }, { 0x5487U, 0x7DU }, { 0x5488U, 0x87U },
    { 0x5489U, 0x91U }, { 0x548AU, 0x9AU }, { 0x548BU, 0xAAU },
    { 0x548CU, 0xB8U }, { 0x548DU, 0xCDU }, { 0x548EU, 0xDDU },
    { 0x548FU, 0xEAU }, { 0x5490U, 0x1DU }, { 0x5580U, 0x06U },
    { 0x5583U, 0x40U }, { 0x5584U, 0x10U }, { 0x5589U, 0x10U },
    { 0x558AU, 0x00U }, { 0x558BU, 0xF8U }, { 0x501DU, 0x04U },
    { 0x5800U, 0x23U }, { 0x5801U, 0x14U }, { 0x5802U, 0x0FU },
    { 0x5803U, 0x0FU }, { 0x5804U, 0x12U }, { 0x5805U, 0x26U },
    { 0x5806U, 0x0CU }, { 0x5807U, 0x08U }, { 0x5808U, 0x05U },
    { 0x5809U, 0x05U }, { 0x580AU, 0x08U }, { 0x580BU, 0x0DU },
    { 0x580CU, 0x08U }, { 0x580DU, 0x03U }, { 0x580EU, 0x00U },
    { 0x580FU, 0x00U }, { 0x5810U, 0x03U }, { 0x5811U, 0x09U },
    { 0x5812U, 0x07U }, { 0x5813U, 0x03U }, { 0x5814U, 0x00U },
    { 0x5815U, 0x01U }, { 0x5816U, 0x03U }, { 0x5817U, 0x08U },
    { 0x5818U, 0x0DU }, { 0x5819U, 0x08U }, { 0x581AU, 0x05U },
    { 0x581BU, 0x06U }, { 0x581CU, 0x08U }, { 0x581DU, 0x0EU },
    { 0x581EU, 0x29U }, { 0x581FU, 0x17U }, { 0x5820U, 0x11U },
    { 0x5821U, 0x11U }, { 0x5822U, 0x15U }, { 0x5823U, 0x28U },
    { 0x5824U, 0x46U }, { 0x5825U, 0x26U }, { 0x5826U, 0x08U },
    { 0x5827U, 0x26U }, { 0x5828U, 0x64U }, { 0x5829U, 0x26U },
    { 0x582AU, 0x24U }, { 0x582BU, 0x22U }, { 0x582CU, 0x24U },
    { 0x582DU, 0x24U }, { 0x582EU, 0x06U }, { 0x582FU, 0x22U },
    { 0x5830U, 0x40U }, { 0x5831U, 0x42U }, { 0x5832U, 0x24U },
    { 0x5833U, 0x26U }, { 0x5834U, 0x24U }, { 0x5835U, 0x22U },
    { 0x5836U, 0x22U }, { 0x5837U, 0x26U }, { 0x5838U, 0x44U },
    { 0x5839U, 0x24U }, { 0x583AU, 0x26U }, { 0x583BU, 0x28U },
    { 0x583CU, 0x42U }, { 0x583DU, 0xCEU }, { 0x5025U, 0x00U },
    { 0x3A0FU, 0x30U }, { 0x3A10U, 0x28U }, { 0x3A1BU, 0x30U },
    { 0x3A1EU, 0x26U }, { 0x3A11U, 0x60U }, { 0x3A1FU, 0x14U },
    { 0x503DU, 0x00U }, { 0x4800U, 0x04U }, { 0x3007U, 0xFBU },
    { 0x3017U, 0x00U }, { 0x301DU, 0xFFU }, { 0x5001U, 0xA3U },
    { 0x3035U, 0x12U }, { 0x3036U, 0xF6U }, { 0x3037U, 0x08U },
    { 0x3108U, 0x12U },
};

/*
 * The wake and geometry: window the full 2592x1944 array and let the ISP
 * scale to QVGA, with the frame timing (HTS 1495, VTS 1121) computed for
 * ~55 fps at the 184.5 MHz system clock the first table set up.
 */
static const cam_regval_t cam_cfg_b[] = {
    { 0x3008U, 0x02U },                     /* wake from software power-down */
    { 0x3C07U, 0x08U }, { 0x3820U, 0x40U }, { 0x3821U, 0x01U },
    { 0x3814U, 0x31U }, { 0x3815U, 0x31U }, { 0x3803U, 0x04U },
    { 0x3800U, 0x00U }, { 0x3801U, 0x00U }, { 0x3802U, 0x00U },
    { 0x3803U, 0x04U }, { 0x3804U, 0x0AU }, { 0x3805U, 0x1FU },
    { 0x3806U, 0x07U }, { 0x3807U, 0x97U }, { 0x3808U, 0x01U },
    { 0x3809U, 0x40U }, { 0x380AU, 0x00U }, { 0x380BU, 0xF0U },
    { 0x380CU, 0x05U }, { 0x380DU, 0xD7U }, { 0x380EU, 0x04U },
    { 0x380FU, 0x61U }, { 0x3813U, 0x06U }, { 0x3618U, 0x00U },
    { 0x3612U, 0x29U }, { 0x3709U, 0x52U }, { 0x370CU, 0x03U },
    { 0x3A02U, 0x03U }, { 0x3A03U, 0xD8U }, { 0x3A09U, 0x27U },
    { 0x3A0AU, 0x00U }, { 0x3A0BU, 0xF6U }, { 0x3A0EU, 0x03U },
    { 0x3A0DU, 0x04U }, { 0x3A14U, 0x03U }, { 0x3A15U, 0xD8U },
    { 0x4004U, 0x02U }, { 0x4713U, 0x03U }, { 0x460BU, 0x35U },
    { 0x460CU, 0x22U }, { 0x4837U, 0x0AU }, { 0x3824U, 0x02U },
    { 0x5001U, 0xA3U },
};

/**
 * @brief Write one bring-up table to the sensor, stopping at a refusal.
 *
 * @param t  Table
 * @param n  Entry count
 * @return TIKU_CAM_OK, or the first failing write's error
 */
static int
cam_write_table(const cam_regval_t *t, uint32_t n)
{
    uint32_t i;
    int rc;

    for (i = 0U; i < n; i++) {
        rc = tiku_camera_arch_write_reg(t[i].reg, t[i].val);
        if (rc != TIKU_CAM_OK) {
            return rc;
        }
    }
    return TIKU_CAM_OK;
}

int
tiku_camera_arch_setup_qvga(void)
{
    uint8_t v;
    int rc;

    /* Software reset.  The sensor resets its own SCCB block the moment 0x82
     * lands, so that write's tail goes unacknowledged BY DESIGN: fire it,
     * ignore the outcome, then rebuild the bus and give the sensor its
     * datasheet settle before holding it in software power-down. */
    rc = tiku_camera_arch_write_reg(0x3103U, 0x11U);
    if (rc != TIKU_CAM_OK) {
        return rc;
    }
    (void)tiku_camera_arch_write_reg(0x3008U, 0x82U);
    tiku_cpu_ra8p1_delay_ms(100U);
    tiku_i2c_arch_close();
    if (tiku_i2c_arch_init(0) != TIKU_I2C_OK) {
        return TIKU_CAM_ERR_STATE;
    }
    rc = tiku_camera_arch_write_reg(0x3008U, 0x42U);
    if (rc != TIKU_CAM_OK) {
        return rc;
    }
    rc = tiku_camera_arch_write_reg(0x3103U, 0x03U);
    if (rc != TIKU_CAM_OK) {
        return rc;
    }

    rc = cam_write_table(cam_cfg_a, sizeof cam_cfg_a / sizeof cam_cfg_a[0]);
    if (rc != TIKU_CAM_OK) {
        return rc;
    }
    /* Virtual channel 0: the two VC bits sit atop other controls. */
    rc = tiku_camera_arch_read_reg(0x4814U, &v);
    if (rc != TIKU_CAM_OK) {
        return rc;
    }
    (void)tiku_camera_arch_write_reg(0x4814U, (uint8_t)(v & ~0xC0U));

    return cam_write_table(cam_cfg_b, sizeof cam_cfg_b / sizeof cam_cfg_b[0]);
}

int
tiku_camera_arch_stream(int on)
{
    /* Frames flow only while 0x4202 is zero; 0x0F gates them at the MIPI
     * packing stage without touching the rest of the pipeline's state. */
    return tiku_camera_arch_write_reg(0x4202U, on ? 0x00U : 0x0FU);
}
