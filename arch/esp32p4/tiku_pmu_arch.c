/*
 * Tiku Operating System v0.06
 * Simple. Persistent. Intelligent.
 * http://tiku-os.org
 *
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 *
 * tiku_pmu_arch.c - Calibrated P4 v1.3 regulators, PSRAM LDO, CPLL and MPLL.
 *
 * CPLL startup first sets the eFuse-calibrated HP and LP regulators.
 * Sequence and settings derived from ESP-IDF 4d59230d rtc_clk_init/clk_tree_ll.
 *
 * SPDX-FileCopyrightText: 2023-2026 Espressif Systems (Shanghai) CO LTD
 * SPDX-License-Identifier: Apache-2.0
 */
#include "tiku_pmu_arch.h"
#include "tiku_esp32p4_regs.h"

#ifndef TIKU_P4_DELAY_XTAL_US
/** @brief Wait at least the requested interval with the CPU at 40 MHz. */
static void delay_xtal_us(uint32_t us)
{
    uint32_t before, now;
    __asm__ volatile("csrr %0, mcycle" : "=r"(before));
    do {
        __asm__ volatile("csrr %0, mcycle" : "=r"(now));
    } while ((uint32_t)(now - before) < us * 40u);
}
#define TIKU_P4_DELAY_XTAL_US(us) delay_xtal_us(us)
#endif

/** @brief Replace selected register bits and preserve the rest. */
static void field(uintptr_t address, uint32_t mask, uint32_t bits)
{
    TIKU_P4_REG_WRITE(address, (TIKU_P4_REG_READ(address) & ~mask) | bits);
}

/** @brief Wait for an analog transaction with a bounded poll count. */
static int analog_idle(void)
{
    unsigned tries;
    for (tries = 0; tries < 4096u; tries++) {
        if (!(TIKU_P4_REG_READ(TIKU_P4_ANALOG_CTRL) & TIKU_P4_ANALOG_BUSY)) {
            return 0;
        }
    }
    return -2;
}

/** @brief Select a regulator, CPLL or MPLL block on the internal analog bus. */
static void analog_select(unsigned block)
{
    uint32_t selected = block == 0x63u ? 1u << 9 : block == 0x6du ? 1u << 10 :
                        block == 0x67u ? 1u << 11 : 1u << 12;
    field(TIKU_P4_ANALOG_CONF2, 0xffffffu, 0);
    field(TIKU_P4_ANALOG_CONF1, 0xffffffu, 0);
    field(TIKU_P4_ANALOG_CONF2, 0xffffffu, selected);
}

/** @brief Read one analog register; return -2 on a bus timeout. */
static int analog_read(unsigned block, unsigned reg, uint32_t *value)
{
    if (analog_idle() != 0) return -2;
    analog_select(block);
    TIKU_P4_REG_WRITE(TIKU_P4_ANALOG_CTRL, block | (reg << 8));
    if (analog_idle() != 0) return -2;
    *value = (TIKU_P4_REG_READ(TIKU_P4_ANALOG_CTRL) >> 16) & 255u;
    return 0;
}

/** @brief Write an analog register and verify the complete byte. */
static int analog_write(unsigned block, unsigned reg, uint32_t value)
{
    uint32_t readback;
    if (analog_idle() != 0) return -2;
    analog_select(block);
    TIKU_P4_REG_WRITE(TIKU_P4_ANALOG_CTRL, block | (reg << 8) |
                      (value << 16) | TIKU_P4_ANALOG_WRITE);
    if (analog_read(block, reg, &readback) != 0 || readback != value) return -2;
    return 0;
}

/** @brief Modify an analog field without changing neighboring fields. */
static int analog_field(unsigned block, unsigned reg, uint32_t mask,
                        uint32_t bits)
{
    uint32_t value;
    if (analog_read(block, reg, &value) != 0) return -2;
    return analog_write(block, reg, (value & ~mask) | bits);
}

/** @brief Decode a calibrated bias, applying the SDK default and saturation. */
static uint32_t calibrated_bias(uint32_t version, uint32_t raw,
                                uint32_t adjustment, uint32_t fallback)
{
    if (version < 2u || version == 100u || raw == 0) return fallback;
    raw += 16u + adjustment;
    return raw > 31u ? 31u : raw;
}

/** @brief Hand calibrated HP/LP regulator settings to PMU, then enable DCDC. */
static int regulators_init(void)
{
    uint32_t version, bias, hp, lp, dcm;
    if (analog_field(0x6d, 10, 3u, 3u) != 0 ||
        analog_field(0x6d, 13, 12u, 0) != 0 ||
        analog_field(0x6a, 4, 15u, 0) != 0) return -2;
    version = TIKU_P4_REG_READ(TIKU_P4_EFUSE_BLOCK_VERSION);
    version = ((version >> 11) & 3u) * 100u + ((version >> 8) & 7u);
    bias = TIKU_P4_REG_READ(TIKU_P4_EFUSE_BIAS);
    hp = calibrated_bias(version, (bias >> 16) & 15u, 0, 24);
    lp = calibrated_bias(version, (bias >> 20) & 15u, 4, 29);
    field(TIKU_P4_PMU_HP_REGULATOR, TIKU_P4_HP_LDO_ENABLE,
          TIKU_P4_HP_LDO_ENABLE);
    field(TIKU_P4_PMU_HP_REGULATOR, 31u << 27, hp << 27);
    field(TIKU_P4_PMU_LP_REGULATOR, 31u << 27, lp << 27);
    dcm = (TIKU_P4_REG_READ(TIKU_P4_PMU_HP_REGULATOR) >> 9) & 31u;
    if (dcm < 27u) dcm = 27u;
    field(TIKU_P4_PMU_DCDC, 1u << 7, 0);
    field(TIKU_P4_PMU_DCDC, 1u, 1u);
    field(TIKU_P4_PMU_DCDC_SWITCH, 3u, 0);
    field(TIKU_P4_PMU_HP_BIAS, 31u << 18, dcm << 18);
    field(TIKU_P4_PMU_HP_REGULATOR, TIKU_P4_PMU_DBIAS_SELECT,
          TIKU_P4_PMU_DBIAS_SELECT);
    if ((TIKU_P4_REG_READ(TIKU_P4_PMU_HP_REGULATOR) >> 27) != hp ||
        (TIKU_P4_REG_READ(TIKU_P4_PMU_LP_REGULATOR) >> 27) != lp ||
        ((TIKU_P4_REG_READ(TIKU_P4_PMU_HP_BIAS) >> 18) & 31u) != dcm ||
        !(TIKU_P4_REG_READ(TIKU_P4_PMU_HP_REGULATOR) &
          TIKU_P4_PMU_DBIAS_SELECT)) return -2;
    TIKU_P4_DELAY_XTAL_US(1000);
    field(TIKU_P4_PMU_HP_REGULATOR, TIKU_P4_HP_LDO_ENABLE, 0);
    return 0;
}

int tiku_p4_pmu_cpll_init(void)
{
    uint32_t gate, conf1, conf2, clock, tries;
    int result;
    if (TIKU_P4_REG_READ(TIKU_P4_ROM_CHIP_ID) != 18u ||
        TIKU_P4_REG_READ(TIKU_P4_ROM_ECO) != 2u ||
        (TIKU_P4_REG_READ(TIKU_P4_ROOT_SELECT) & 3u) != 0 ||
        (TIKU_P4_REG_READ(TIKU_P4_ROOT_DIV0) & 0x1ffffff0u) != 0) return -1;
    gate = TIKU_P4_REG_READ(TIKU_P4_LP_CLK);
    TIKU_P4_REG_WRITE(TIKU_P4_LP_CLK, gate | TIKU_P4_ANALOG_ENABLE);
    conf1 = TIKU_P4_REG_READ(TIKU_P4_ANALOG_CONF1);
    conf2 = TIKU_P4_REG_READ(TIKU_P4_ANALOG_CONF2);
    clock = TIKU_P4_REG_READ(TIKU_P4_ANALOG_CLK);
    TIKU_P4_REG_WRITE(TIKU_P4_ANALOG_CLK, clock | 1u);
    result = regulators_init();
    if (result == 0) {
        TIKU_P4_REG_WRITE(TIKU_P4_PMU_CLOCK_POWER, TIKU_P4_CPLL_POWER_UP);
        TIKU_P4_REG_WRITE(TIKU_P4_PMU_CLOCK_POWER, TIKU_P4_CPLL_CLOCK_UP);
        field(TIKU_P4_PLL_CAL, TIKU_P4_CPLL_CAL_STOP, 0);
        result = analog_write(0x67, 2, 0x50);
        if (result == 0) result = analog_write(0x67, 3, 9);
        if (result == 0) result = analog_write(0x67, 6, 0x73);
        if (result == 0) {
            result = -3;
            for (tries = 0; tries < 10000u; tries++) {
                if (TIKU_P4_REG_READ(TIKU_P4_PLL_CAL) & TIKU_P4_CPLL_CAL_END) {
                    TIKU_P4_DELAY_XTAL_US(10);
                    result = 0;
                    break;
                }
            }
        }
        field(TIKU_P4_PLL_CAL, TIKU_P4_CPLL_CAL_STOP, TIKU_P4_CPLL_CAL_STOP);
        if (result != 0) {
            TIKU_P4_REG_WRITE(TIKU_P4_PMU_CLOCK_POWER, TIKU_P4_CPLL_CLOCK_DOWN);
            TIKU_P4_REG_WRITE(TIKU_P4_PMU_CLOCK_POWER, TIKU_P4_CPLL_POWER_DOWN);
        }
    }
    TIKU_P4_REG_WRITE(TIKU_P4_ANALOG_CONF1, conf1);
    TIKU_P4_REG_WRITE(TIKU_P4_ANALOG_CONF2, conf2);
    TIKU_P4_REG_WRITE(TIKU_P4_ANALOG_CLK, clock);
    TIKU_P4_REG_WRITE(TIKU_P4_LP_CLK, gate);
    return result;
}

/* LDO VO2 fields (pmu_struct.h pmu_ext_ldo, pmu_ext_ldo_ana). */
#define LDO_FORCE_TIEH_SEL (1u << 7)        /* software owns the setting */
#define LDO_XPD            (1u << 8)
#define LDO_TIEH_SEL       (7u << 9)
#define LDO_TIEH           (1u << 14)       /* 1: rail, 0: Vref x mul */
#define LDO_MUL_POS        23u
#define LDO_EN_VDET        (1u << 26)
#define LDO_EN_CUR_LIM     (1u << 27)
#define LDO_DREF_POS       28u
#define LDO_SETTING        ((15u << LDO_DREF_POS) | (7u << LDO_MUL_POS))

int tiku_p4_pmu_psram_ldo_init(void)
{
    uint32_t sys2 = TIKU_P4_REG_READ(TIKU_P4_EFUSE_BLOCK_VERSION);
    uint32_t sys3 = TIKU_P4_REG_READ(TIKU_P4_EFUSE_SYS3);
    uint32_t version = ((sys2 >> 11) & 3u) * 100u + ((sys2 >> 8) & 7u);
    /* 1800 mV: the eFuse dref and mul of a calibrated part, else the match
     * ldo_ll_voltage_to_dref_mul() finds with the default constants. */
    uint32_t dref = 8u, mul = 4u, setting;

    if (version >= 1u && (sys2 >> 28) != 0u && ((sys3 >> 3) & 7u) != 0u) {
        dref = sys2 >> 28;
        mul = (sys3 >> 3) & 7u;
    }
    setting = (dref << LDO_DREF_POS) | (mul << LDO_MUL_POS);
    /* esp_ldo_acquire_channel(): current limit on across the change. */
    field(TIKU_P4_PSRAM_LDO_ANA, LDO_SETTING | LDO_EN_CUR_LIM,
          setting | LDO_EN_CUR_LIM);
    field(TIKU_P4_PSRAM_LDO, LDO_TIEH | LDO_TIEH_SEL | LDO_FORCE_TIEH_SEL,
          LDO_FORCE_TIEH_SEL);
    field(TIKU_P4_PSRAM_LDO_ANA, LDO_EN_VDET, LDO_EN_VDET);
    field(TIKU_P4_PSRAM_LDO, LDO_XPD, LDO_XPD);
    field(TIKU_P4_PSRAM_LDO_ANA, LDO_EN_CUR_LIM, 0);
    TIKU_P4_DELAY_XTAL_US(200);
    if (!(TIKU_P4_REG_READ(TIKU_P4_PSRAM_LDO) & LDO_XPD) ||
        (TIKU_P4_REG_READ(TIKU_P4_PSRAM_LDO_ANA) & LDO_SETTING) != setting)
        return -2;
    return 0;
}

int tiku_p4_pmu_mpll_init(void)
{
    uint32_t gate, conf1, conf2, clock, tries;
    int result = -2;
    if (TIKU_P4_REG_READ(TIKU_P4_ROM_CHIP_ID) != 18u ||
        TIKU_P4_REG_READ(TIKU_P4_ROM_ECO) != 2u ||
        (TIKU_P4_REG_READ(TIKU_P4_ROOT_SELECT) & 3u) != 0 ||
        (TIKU_P4_REG_READ(TIKU_P4_ROOT_DIV0) & 0x1ffffff0u) != 0) return -1;
    gate = TIKU_P4_REG_READ(TIKU_P4_LP_CLK);
    conf1 = TIKU_P4_REG_READ(TIKU_P4_ANALOG_CONF1);
    conf2 = TIKU_P4_REG_READ(TIKU_P4_ANALOG_CONF2);
    clock = TIKU_P4_REG_READ(TIKU_P4_ANALOG_CLK);
    TIKU_P4_REG_WRITE(TIKU_P4_LP_CLK, gate | TIKU_P4_ANALOG_ENABLE);
    TIKU_P4_REG_WRITE(TIKU_P4_ANALOG_CLK, clock | 1u);
    field(0x5011515cu, 1u << 24, 1u << 24);
    field(TIKU_P4_ROOT_SELECT, 1u << 28, 1u << 28);
    field(TIKU_P4_PLL_CAL, 1u << 9, 0);
    if (!analog_field(0x63, 3, 0x30, 0x30) &&
        !analog_field(0x63, 1, 0x20, 0) &&
        !analog_field(0x63, 1, 0x20, 0x20) &&
        !analog_write(0x63, 2, 0x79)) {
        result = -3;
        for (tries = 0; tries < 10000; tries++) {
            if (TIKU_P4_REG_READ(TIKU_P4_PLL_CAL) & (1u << 8)) {
                TIKU_P4_DELAY_XTAL_US(10);
                result = 0;
                break;
            }
        }
    }
    field(TIKU_P4_PLL_CAL, 1u << 9, 1u << 9);
    if (result) {
        field(TIKU_P4_ROOT_SELECT, 1u << 28, 0);
        field(0x5011515cu, 1u << 24, 0);
    }
    TIKU_P4_REG_WRITE(TIKU_P4_ANALOG_CONF1, conf1);
    TIKU_P4_REG_WRITE(TIKU_P4_ANALOG_CONF2, conf2);
    TIKU_P4_REG_WRITE(TIKU_P4_ANALOG_CLK, clock);
    TIKU_P4_REG_WRITE(TIKU_P4_LP_CLK, gate);
    return result;
}
