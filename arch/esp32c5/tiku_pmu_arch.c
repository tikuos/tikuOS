/*
 * Tiku Operating System v0.06
 * Simple. Ubiquitous. Intelligence, Everywhere.
 * http://tiku-os.org
 *
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 *
 * tiku_pmu_arch.c - C5 digital core voltage for the CPU clock.
 *
 * The image replaces ESP-IDF's second-stage bootloader, which sets the
 * eFuse-calibrated regulator level before raising the CPU clock
 * (rtc_clk_init.c, pmu_param.c and regi2c_impl.c at ESP-IDF 4d59230).
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include "tiku_pmu_arch.h"
#include "tiku_esp32c5_regs.h"
#include "tiku_irq_arch.h"

/* eFuse words: wafer and block versions, then the regulator calibration. */
#define C5_EFUSE_SYS2       0x600B484Cu
#define C5_EFUSE_SYS3       0x600B4850u

/* PMU regulator registers; the DBIAS field is bits 31:27 in each. */
#define C5_PMU_HP_ACTIVE    0x600B0028u
#define C5_PMU_HP_MODEM     0x600B005Cu
#define C5_PMU_HP_SLEEP_LP  0x600B009Cu
#define C5_PMU_DBIAS_SHIFT  27u
#define C5_PMU_DBIAS_SEL    (1u << 14)

/* Analog I2C master (modem LP control clock, block select, transfer). */
#define C5_ANA_CLOCK        0x600AF018u
#define C5_ANA_MST          0x600AF800u
#define C5_ANA_CONF1        (C5_ANA_MST + 0x1Cu)
#define C5_ANA_CONF2        (C5_ANA_MST + 0x20u)
#define C5_ANA_BUSY         (1u << 25)
#define C5_ANA_WRITE        (1u << 24)
#define C5_ANA_SPINS        200000u

/* Digital regulator block on the analog bus, and the fields rtc_clk_init
 * changes: register, bit. */
#define C5_DIG_REG          0x6Du
#define C5_DIG_REG_SEL      (1u << 12)
#define C5_DIG_REG_RD_MASK  (0xFFFFFFu & ~(1u << 10))

static int8_t voltage_result = 1;   /* 1 = not attempted yet */
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
 * @brief Set bit @p bit of digital-regulator register @p reg to @p value.
 * @return 0, or -1 when the analog bus stays busy.
 */
static int dig_reg_bit(uint8_t reg, unsigned bit, unsigned value)
{
    uint32_t control = C5_ANA_MST +
        ((TIKU_C5_REG_READ(C5_ANA_CONF2) & C5_DIG_REG_SEL) ? 0u : 4u);
    uint32_t data;

    TIKU_C5_REG_WRITE(C5_ANA_CONF1, C5_DIG_REG_RD_MASK);
    if (analog_idle(control) != 0) {
        return -1;
    }
    TIKU_C5_REG_WRITE(control, C5_DIG_REG | ((uint32_t)reg << 8));
    if (analog_idle(control) != 0) {
        return -1;
    }
    data = (TIKU_C5_REG_READ(control) >> 16) & 0xFFu;
    data = (data & ~(1u << bit)) | ((value & 1u) << bit);
    TIKU_C5_REG_WRITE(control, C5_DIG_REG | ((uint32_t)reg << 8) |
                               C5_ANA_WRITE | (data << 16));
    return analog_idle(control);
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
    TIKU_C5_REG_WRITE(address,
        (TIKU_C5_REG_READ(address) & ~(31u << C5_PMU_DBIAS_SHIFT)) |
        (dbias << C5_PMU_DBIAS_SHIFT));
}

int tiku_c5_core_voltage_ready(void)
{
    uint32_t state, clock, hp, lp;

    if (voltage_result <= 0) {
        return voltage_result;
    }
    state = TIKU_C5_IRQ_SAVE();
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
