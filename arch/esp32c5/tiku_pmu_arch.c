/*
 * Tiku Operating System v0.06
 * Simple. Ubiquitous. Intelligence, Everywhere.
 * http://tiku-os.org
 *
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 *
 * tiku_pmu_arch.c - C5 core voltage and PLL, set before the CPU clock.
 *
 * Replaces ESP-IDF's bootloader (eFuse-calibrated regulator level, 480 MHz
 * PLL before the CPU clock rises) and the application start's hand-over of
 * the regulator to the PVT monitor (rtc_clk_init.c, pmu_pvt.c, 4d59230).
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include "tiku_pmu_arch.h"
#include "tiku_esp32c5_regs.h"
#include "tiku_irq_arch.h"
#include "tiku_cpu_common.h"

/* eFuse words: wafer and block versions, then the regulator calibration. */
#define C5_EFUSE_SYS2 0x600B484Cu
#define C5_EFUSE_SYS3 0x600B4850u

/* PMU regulator registers; the DBIAS field is bits 31:27 in each. */
#define C5_PMU_HP_ACTIVE   0x600B0028u
#define C5_PMU_HP_MODEM    0x600B005Cu
#define C5_PMU_HP_SLEEP_LP 0x600B009Cu
#define C5_PMU_DBIAS_SHIFT 27u
#define C5_PMU_DBIAS_SEL   (1u << 14)

/* Analog I2C master (modem LP control clock, block select, transfer). */
#define C5_ANA_CLOCK 0x600AF018u
#define C5_ANA_MST   0x600AF800u
#define C5_ANA_CONF1 (C5_ANA_MST + 0x1Cu)
#define C5_ANA_CONF2 (C5_ANA_MST + 0x20u)
#define C5_ANA_BUSY  (1u << 25)
#define C5_ANA_WRITE (1u << 24)
#define C5_ANA_SPINS 200000u

/* PMU immediate HP clock power: the PLL with its analog interface, and the
 * PLL's clock gate.  Analog master CONF0 holds the PLL calibration control
 * bits and its done flag. */
#define C5_PMU_IMM_CK_POWER (0x600B0000u + 0xCCu)
#define C5_PLL_POWER_ON     ((1u << 28) | (1u << 29) | (1u << 30))
#define C5_PLL_GATE_OPEN    (1u << 25)
#define C5_PLL_POWER_OFF    ((1u << 4) | (1u << 5))
#define C5_PLL_GATE_CLOSED  (1u << 0)
#define C5_ANA_CONF0        (C5_ANA_MST + 0x18u)
#define C5_PLL_STOP_HIGH    (1u << 2)
#define C5_PLL_STOP_LOW     (1u << 3)
#define C5_PLL_CAL_DONE     (1u << 24)
#define C5_PCR_SYSCLK       0x60096110u

/* Modem clock gating (rtc_clk_init's ICG map preinit): the PMU's
 * active-state ICG code, the modem APB, analog master and LP APB gate maps
 * keyed by that code, the triggers that apply them, and the analog master's
 * power bits in PMU immediate clock power. */
#define C5_PMU_ACTIVE_ICG  (0x600B0000u + 0x0Cu)
#define C5_ICG_CODE_ACTIVE 2u
#define C5_SYSCON_POWER_ST 0x600A9C0Cu
#define C5_LPCON_POWER_ST  0x600AF020u
#define C5_PMU_ICG_UPDATE  (0x600B0000u + 0xDCu)
#define C5_PMU_ICG_SWITCH  (0x600B0000u + 0xD0u)
#define C5_ANA_POWER_ON    ((1u << 28) | (1u << 29))

/* A block on the analog bus: its address, the CONF2 bit that selects its
 * control register, and the CONF1 mask regi2c_impl.c writes before a
 * transfer. */
typedef struct {
    uint8_t address;
    uint32_t select;
    uint32_t read_mask;
} analog_block_t;

/* The digital regulator and the PLL. */
static const analog_block_t dig_reg = {0x6Du, 1u << 12,
                                       0xFFFFFFu & ~(1u << 10)};
static const analog_block_t bbpll = {0x66u, 1u << 9, 0xFFFFFFu & ~(1u << 7)};

static int8_t voltage_result = 1; /* 1 = not attempted yet */
static int8_t pll_result = 1;     /* 1 = not attempted yet */
static uint32_t applied_dbias;

/** @brief Wait a bounded number of reads for the analog bus to go idle. */
static int analog_idle(uint32_t control)
{
    unsigned n;

    for (n = 0; n < C5_ANA_SPINS; n++) {
        if (!(TIKU_C5_REG_READ(control) & C5_ANA_BUSY)) {
            return 0;
        }
    }
    return -1;
}

/**
 * @brief Open the modem clock gates for the active state and power the
 *        analog bus; after a cold boot the ROM leaves both closed, and a
 *        transfer then reads zeros and writes nothing.
 */
static void analog_bus_open(void)
{
    TIKU_C5_REG_WRITE(C5_PMU_ACTIVE_ICG,
                      (TIKU_C5_REG_READ(C5_PMU_ACTIVE_ICG) & ~(3u << 30)) |
                          (C5_ICG_CODE_ACTIVE << 30));
    TIKU_C5_REG_WRITE(C5_SYSCON_POWER_ST,
                      TIKU_C5_REG_READ(C5_SYSCON_POWER_ST) |
                          (1u << (28u + C5_ICG_CODE_ACTIVE)));
    TIKU_C5_REG_WRITE(C5_LPCON_POWER_ST,
                      TIKU_C5_REG_READ(C5_LPCON_POWER_ST) |
                          (1u << (24u + C5_ICG_CODE_ACTIVE)) |
                          (1u << (28u + C5_ICG_CODE_ACTIVE)));
    TIKU_C5_REG_WRITE(C5_PMU_ICG_UPDATE, 1u << 31);
    TIKU_C5_REG_WRITE(C5_PMU_ICG_SWITCH, 1u << 28);
    TIKU_C5_REG_WRITE(C5_PMU_IMM_CK_POWER,
                      TIKU_C5_REG_READ(C5_PMU_IMM_CK_POWER) | C5_ANA_POWER_ON);
}

/**
 * @brief Set bits @p msb..@p lsb of register @p reg of block @p b to
 *        @p value, keeping the register's other bits.
 * @return 0, or -1 when the analog bus stays busy.
 */
static int analog_field(const analog_block_t *b, uint8_t reg, unsigned msb,
                        unsigned lsb, unsigned value)
{
    uint32_t control = C5_ANA_MST +
                       ((TIKU_C5_REG_READ(C5_ANA_CONF2) & b->select) ? 0u : 4u);
    uint32_t word = b->address | ((uint32_t)reg << 8);
    uint32_t mask = ((2u << (msb - lsb)) - 1u) << lsb;
    uint32_t data;

    TIKU_C5_REG_WRITE(C5_ANA_CONF1, b->read_mask);
    if (analog_idle(control) != 0) {
        return -1;
    }
    TIKU_C5_REG_WRITE(control, word);
    if (analog_idle(control) != 0) {
        return -1;
    }
    data = (TIKU_C5_REG_READ(control) >> 16) & 0xFFu;
    data = (data & ~mask) | ((value << lsb) & mask);
    TIKU_C5_REG_WRITE(control, word | C5_ANA_WRITE | (data << 16));
    return analog_idle(control);
}

/** @brief Set bit @p bit of digital-regulator register @p reg to @p value. */
static int dig_reg_bit(uint8_t reg, unsigned bit, unsigned value)
{
    return analog_field(&dig_reg, reg, bit, bit, value);
}

/**
 * @brief Calibrated setting from a 4-bit eFuse field, as pmu_param.c does.
 *
 * Wafer v0.1 with block v0.1, or wafer v1.0 and later with block v0.2 and
 * later, carry a calibration; a nonzero value plus 19 (16 to centre it, 3
 * for the CPU clock switch) is the setting, at most 31.
 */
static uint32_t calibrated(unsigned shift)
{
    uint32_t sys2 = TIKU_C5_REG_READ(C5_EFUSE_SYS2);
    uint32_t chip = ((sys2 >> 4) & 3u) * 100u + (sys2 & 15u);
    uint32_t block = ((sys2 >> 11) & 3u) * 100u + ((sys2 >> 8) & 7u);
    uint32_t fuse = 0;

    if ((chip == 1u && block >= 1u) || (chip >= 100u && block >= 2u)) {
        fuse = (TIKU_C5_REG_READ(C5_EFUSE_SYS3) >> shift) & 15u;
    }
    if (fuse == 0) {
        return TIKU_C5_PMU_DBIAS_DEFAULT;
    }
    return fuse + 19u > 31u ? 31u : fuse + 19u;
}

/** @brief Replace the DBIAS field of regulator register @p address. */
static void set_dbias(uint32_t address, uint32_t dbias)
{
    TIKU_C5_REG_WRITE(
        address, (TIKU_C5_REG_READ(address) & ~(31u << C5_PMU_DBIAS_SHIFT)) |
                     (dbias << C5_PMU_DBIAS_SHIFT));
}

int tiku_c5_core_voltage_ready(void)
{
    uint32_t state, clock, hp, lp;

    if (voltage_result <= 0) {
        return voltage_result;
    }
    state = TIKU_C5_IRQ_SAVE();
    analog_bus_open();
    clock = TIKU_C5_REG_READ(C5_ANA_CLOCK);
    TIKU_C5_REG_WRITE(C5_ANA_CLOCK, clock | 4u);
    /* Regulator control through the PMU, not the analog bus (rtc_clk_init:
     * ENIF_RTC_DREG and ENIF_DIG_DREG on, XPD_RTC_REG and XPD_DIG_REG off). */
    if (dig_reg_bit(5u, 7u, 1u) != 0 || dig_reg_bit(7u, 7u, 1u) != 0 ||
        dig_reg_bit(13u, 2u, 0u) != 0 || dig_reg_bit(13u, 3u, 0u) != 0) {
        TIKU_C5_REG_WRITE(C5_ANA_CLOCK, clock);
        voltage_result = -1;
        TIKU_C5_IRQ_RESTORE(state);
        return -1;
    }
    TIKU_C5_REG_WRITE(C5_ANA_CLOCK, clock);
    hp = calibrated(10u);
    lp = calibrated(14u);
    TIKU_C5_REG_WRITE(C5_PMU_HP_ACTIVE,
                      TIKU_C5_REG_READ(C5_PMU_HP_ACTIVE) | C5_PMU_DBIAS_SEL);
    set_dbias(C5_PMU_HP_ACTIVE, hp);
    set_dbias(C5_PMU_HP_MODEM, hp);
    set_dbias(C5_PMU_HP_SLEEP_LP, lp);
    applied_dbias = hp;
    voltage_result = 0;
    TIKU_C5_IRQ_RESTORE(state);
    return 0;
}

uint32_t tiku_c5_core_voltage_dbias(void)
{
    return applied_dbias;
}

/**
 * @brief Program the PLL for 480 MHz from the crystal: reference and
 *        feedback dividers, charge pump and bias, as clk_ll_bbpll_set_config
 *        does for a 48 or 40 MHz crystal.
 */
static int pll_configure(unsigned xtal)
{
    unsigned feedback = xtal == 48u ? 10u : 12u;
    unsigned dr = xtal == 48u ? 1u : 0u;

    return analog_field(&bbpll, 2u, 7u, 0u, (5u << 4) | 1u) != 0 ||
                   analog_field(&bbpll, 3u, 7u, 0u, feedback) != 0 ||
                   analog_field(&bbpll, 5u, 2u, 0u, dr) != 0 ||
                   analog_field(&bbpll, 5u, 6u, 4u, dr) != 0 ||
                   analog_field(&bbpll, 6u, 7u, 6u, 1u) != 0 ||
                   analog_field(&bbpll, 6u, 5u, 4u, 3u) != 0 ||
                   analog_field(&bbpll, 9u, 1u, 0u, 3u) != 0
               ? -1
               : 0;
}

int tiku_c5_pll_ready(void)
{
    unsigned xtal = (TIKU_C5_REG_READ(C5_PCR_SYSCLK) >> 24) & 127u;
    uint32_t state, clock, power;
    unsigned n;
    int failed;

    if (pll_result <= 0) {
        return pll_result;
    }
    if (xtal != 48u && xtal != 40u) {
        pll_result = -1;
        return -1;
    }
    state = TIKU_C5_IRQ_SAVE();
    analog_bus_open();
    power = TIKU_C5_REG_READ(C5_PMU_IMM_CK_POWER);
    TIKU_C5_REG_WRITE(C5_PMU_IMM_CK_POWER, power | C5_PLL_POWER_ON);
    power = TIKU_C5_REG_READ(C5_PMU_IMM_CK_POWER);
    TIKU_C5_REG_WRITE(C5_PMU_IMM_CK_POWER, power | C5_PLL_GATE_OPEN);
    clock = TIKU_C5_REG_READ(C5_ANA_CLOCK);
    TIKU_C5_REG_WRITE(C5_ANA_CLOCK, clock | 4u);
    /* Calibration runs while STOP_LOW is set and ends with STOP_HIGH. */
    TIKU_C5_REG_WRITE(C5_ANA_CONF0,
                      (TIKU_C5_REG_READ(C5_ANA_CONF0) & ~C5_PLL_STOP_HIGH) |
                          C5_PLL_STOP_LOW);
    failed = pll_configure(xtal);
    for (n = 0; !failed && n < C5_ANA_SPINS; n++) {
        if (TIKU_C5_REG_READ(C5_ANA_CONF0) & C5_PLL_CAL_DONE) {
            break;
        }
    }
    if (!failed && n == C5_ANA_SPINS) {
        failed = 1;
    }
    if (!failed) {
        tiku_cpu_c5_delay_us(10);
    }
    TIKU_C5_REG_WRITE(C5_ANA_CONF0,
                      (TIKU_C5_REG_READ(C5_ANA_CONF0) & ~C5_PLL_STOP_LOW) |
                          C5_PLL_STOP_HIGH);
    TIKU_C5_REG_WRITE(C5_ANA_CLOCK, clock);
    if (failed) {
        power = TIKU_C5_REG_READ(C5_PMU_IMM_CK_POWER);
        TIKU_C5_REG_WRITE(C5_PMU_IMM_CK_POWER, power | C5_PLL_GATE_CLOSED);
        power = TIKU_C5_REG_READ(C5_PMU_IMM_CK_POWER);
        TIKU_C5_REG_WRITE(C5_PMU_IMM_CK_POWER, power | C5_PLL_POWER_OFF);
    }
    pll_result = failed ? -1 : 0;
    TIKU_C5_IRQ_RESTORE(state);
    return pll_result;
}

int tiku_c5_pll_result(void)
{
    return pll_result;
}

/* The PVT monitor (ESP-IDF pmu_pvt.c and rtc.h): its clocks in PCR, its
 * dbias command, channel and timer registers, the monitor cell the
 * regulator follows, and the charge pump; the eFuse LP-to-HP gap word. */
#define C5_PCR_PVT_CONF     0x600960B8u
#define C5_PCR_PVT_FUNC     0x600960BCu
#define C5_PVT_BASE         0x60019000u
#define C5_PVT_PUMP_BITMAP  (C5_PVT_BASE + 0x14u)
#define C5_PVT_PUMP_DRV     (C5_PVT_BASE + 0x28u)
#define C5_PVT_PUMP_CHANNEL (C5_PVT_BASE + 0x2Cu)
#define C5_PVT_CLK_CFG      (C5_PVT_BASE + 0x30u)
#define C5_PVT_CHANNEL_SEL  (C5_PVT_BASE + 0x34u)
#define C5_PVT_CHANNEL_CFG  (C5_PVT_BASE + 0x3Cu)
#define C5_PVT_CMD          (C5_PVT_BASE + 0x50u)
#define C5_PVT_TIMER        (C5_PVT_BASE + 0x64u)
#define C5_PVT_CELL_CONF    (C5_PVT_BASE + 0xD8u)
#define C5_PMU_DBIAS_INIT   (1u << 3)
#define C5_EFUSE_SYS4       0x600B4854u

static int8_t pvt_result = 1;       /* 1 = not attempted yet */

/** @brief Replace the field @p mask of register @p address with @p value. */
static void pvt_field(uint32_t address, uint32_t mask, uint32_t value)
{
    TIKU_C5_REG_WRITE(address, (TIKU_C5_REG_READ(address) & ~mask) |
                                   (value & mask));
}

/**
 * @brief The LP-to-HP regulator offset for the PVT commands from the eFuse
 *        gap (sign in bit 4, magnitude below), as pmu_pvt.c derives it.
 */
static uint32_t pvt_gap(void)
{
    uint32_t word = (TIKU_C5_REG_READ(C5_EFUSE_SYS4) >> 1) & 31u;
    int gap = (word & 16u) ? -(int)(word & 15u) : (int)(word & 15u);

    gap -= 8;
    if (gap < 0) {
        gap = gap >= -15 ? 16 - gap : 31;
    }
    return (uint32_t)gap;
}

/* The hand-over runs once the CPU is on the PLL and touches no clock, so an
 * XIP image keeps its code out of SRAM. */
#if TIKU_ESP32C5_XIP_CODE
#define PVT_CODE __attribute__((section(".xip.roots")))
#else
#define PVT_CODE
#endif

PVT_CODE int tiku_c5_pvt_ready(void)
{
    uint32_t sys2 = TIKU_C5_REG_READ(C5_EFUSE_SYS2);
    uint32_t block = ((sys2 >> 11) & 3u) * 100u + ((sys2 >> 8) & 7u);
    uint32_t state, gap, i;

    if (pvt_result != 1) {
        return pvt_result;
    }
    if (block < 2u) {
        pvt_result = 2;
        return 2;
    }
    state = TIKU_C5_IRQ_SAVE();
    /* Monitor clocks: reset pulse, then the monitor and function clocks. */
    pvt_field(C5_PCR_PVT_CONF, 1u << 1, 1u << 1);
    pvt_field(C5_PCR_PVT_CONF, 1u << 1, 0);
    pvt_field(C5_PCR_PVT_CONF, 1u << 0, 1u << 0);
    pvt_field(C5_PCR_PVT_FUNC, 1u << 22, 1u << 22);
    pvt_field(C5_PVT_TIMER, 1u << 31, 0);
    tiku_cpu_c5_delay_us(1);
    /* Monitor cells 33 and 37, their filters, the three dbias commands and
     * the regulation period (rtc.h's PVT_* values). */
    pvt_field(C5_PVT_CHANNEL_SEL, (127u << 25) | (127u << 18),
              (33u << 25) | (37u << 18));
    pvt_field(C5_PVT_CHANNEL_CFG, 0x1FFFFu, 0x11FFFu);
    pvt_field(C5_PVT_CHANNEL_CFG + 4u, 0x1FFFFu, 0x17FFFu);
    pvt_field(C5_PVT_CHANNEL_CFG + 8u, 0x1FFFFu, 0x10000u);
    pvt_field(C5_PVT_CMD, 0x7FFu, 0x24u);
    pvt_field(C5_PVT_CMD + 4u, 0x7FFu, 0x5u);
    pvt_field(C5_PVT_CMD + 8u, 0x7FFu, 0x427u);
    pvt_field(C5_PVT_TIMER, 0xFFFFu << 15, 0xFFFFu << 15);
    pvt_field(C5_PCR_PVT_FUNC, 15u, 1u);
    pvt_field(C5_PCR_PVT_FUNC, 1u << 20, 1u << 20);
    /* Delay limits of monitor cell site 2 (too high, too low, pump). */
    pvt_field(C5_PVT_CELL_CONF, 255u << 2, 157u << 2);
    pvt_field(C5_PVT_CELL_CONF + 4u, 255u << 2, 147u << 2);
    pvt_field(C5_PVT_CELL_CONF + 8u, 255u << 2, 139u << 2);
    gap = pvt_gap();
    for (i = 0; i < 3u; i++) {
        pvt_field(C5_PVT_CMD + 4u * i, (1u << 16) | (15u << 11),
                  ((gap >> 4) << 16) | ((gap & 15u) << 11));
    }
    /* The charge pump: channel code 1, monitor cell 22, drive 0. */
    pvt_field(C5_PVT_PUMP_CHANNEL, 31u << 27, 1u << 27);
    TIKU_C5_REG_WRITE(C5_PVT_PUMP_BITMAP, 1u << 22);
    pvt_field(C5_PVT_PUMP_DRV, 15u << 27, 0);
    /* Tracking starts from the static setting, then the regulator follows
     * the monitor instead of the PMU field. */
    pvt_field(C5_PMU_HP_ACTIVE, C5_PMU_DBIAS_INIT, C5_PMU_DBIAS_INIT);
    pvt_field(C5_PCR_PVT_FUNC, 1u << 22, 1u << 22);
    pvt_field(C5_PCR_PVT_CONF, 1u << 0, 1u << 0);
    pvt_field(C5_PVT_CLK_CFG, 1u << 8, 1u << 8);
    pvt_field(C5_PVT_CELL_CONF, 1u << 0, 1u << 0);
    tiku_cpu_c5_delay_us(10);
    pvt_field(C5_PMU_HP_ACTIVE, C5_PMU_DBIAS_SEL, 0);
    pvt_field(C5_PMU_HP_ACTIVE, C5_PMU_DBIAS_INIT, 0);
    pvt_field(C5_PVT_TIMER, 1u << 31, 1u << 31);
    tiku_cpu_c5_delay_us(50);
    pvt_field(C5_PVT_PUMP_DRV, 1u << 9, 1u << 9);
    pvt_result = 0;
    TIKU_C5_IRQ_RESTORE(state);
    return 0;
}

int tiku_c5_pvt_result(void)
{
    return pvt_result;
}
