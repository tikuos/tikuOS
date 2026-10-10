/*
 * Tiku Operating System v0.06
 * Simple. Ubiquitous. Intelligence, Everywhere.
 * http://tiku-os.org
 *
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 *
 * tiku_pmu_arch.c - C5 core voltage and PLL, set before the CPU clock.
 *
 * The image replaces ESP-IDF's second-stage bootloader, which sets the
 * eFuse-calibrated regulator level and brings the 480 MHz PLL up before
 * raising the CPU clock (rtc_clk_init.c and rtc_clk.c at ESP-IDF 4d59230).
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
