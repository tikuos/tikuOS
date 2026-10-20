/*
 * Tiku Operating System v0.06
 * Simple. Persistent. Intelligent.
 * http://tiku-os.org
 *
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 *
 * tiku_adc_arch.c - P4 SAR ADC one-shot conversions, the temperature sensor.
 *
 * Register sequence: ESP-IDF 4d59230 adc_ll.h, adc_oneshot_hal.c,
 * adc_hal_common.c, adc_share_hw_ctrl.c, sar_periph_ctrl.c, regi2c_ctrl_ll.h,
 * regi2c_saradc.h, temperature_sensor_ll.h, sar_tsens_ctrl.c, eFuse table.
 *
 * SPDX-License-Identifier: Apache-2.0
 */
#include <stddef.h>
#include "tiku_adc_arch.h"
#include "tiku_esp32p4_regs.h"
#include "tiku_irq_arch.h"
#include "tiku_cpu_common.h"
#include "tiku_gpio_arch.h"
#include <interfaces/gpio/tiku_gpio_owner.h>
#if TIKU_LPCORE_ENABLE
#include "tiku_lpcore_arch.h"
#include "lpcore/tiku_lpcore_ipc.h"
#define SAR_TAKE() tiku_p4_lpcore_sar_take()
#define SAR_GIVE() tiku_p4_lpcore_sar_give()
#else
#define SAR_TAKE() ((void)0)
#define SAR_GIVE() ((void)0)
#endif

/* LP ADC, the RTC controller of both SAR units.  Per unit: the reader (clock
 * divider 7:0, data inversion bit 28 or 29), the one-shot register (result
 * 15:0, done 16, start 17, start force 18, pad map 30:19, pad force 31), the
 * controller select (bit 31) and two attenuation bits per pad. */
#define LP_ADC          0x50127000u
#define READER(u)       (LP_ADC + ((u) ? 0x24u : 0x00u))
#define ONESHOT(u)      (LP_ADC + ((u) ? 0x30u : 0x0Cu))
#define SELECT(u)       (LP_ADC + ((u) ? 0x34u : 0x10u))
#define ATTEN(u)        (LP_ADC + ((u) ? 0x38u : 0x14u))
#define FORCE_XPD       (LP_ADC + 0x3Cu)
#define INVERT(u)       ((u) ? 1u << 29 : 1u << 28)
#define DONE            (1u << 16)
#define START           (1u << 17)
#define START_FORCE     (1u << 18)
#define PAD_POS         19u
#define PAD_FORCE       (1u << 31)
#define SELECT_RTC2     (1u << 31)          /* ADC2: the RTC controller */
#define PWDET_CCT       (7u << 28)          /* ADC2: PA monitor capacitor */
#define XPD_ON          15u                 /* both units powered up */
/* The temperature sensor: output 7:0, power-up bit 22. */
#define TSENS           0x5012F000u
#define TSENS_PU        (1u << 22)
#define TSENS_MIDDLE    15u
#define TSENS_SETTLE_US 300u
/* LPPERI clock gates and resets, the analog bus's power. */
#define LP_CLOCKS       ((1u << 17) | (1u << 23) | (1u << 27))
#define LP_TSENS_RESET  (1u << 18)
#define SAR_POWER       0x5011515Cu         /* PMU_RF_PWC: 26 reset, 27 on */
/* The digital ADC, which ADC2 conversions need clocked: APB gate, function
 * gate and reset; its control register's clock gate and SAR2 power force. */
#define DIG_APB         TIKU_P4_SOC_CLK2
#define DIG_APB_ON      (1u << 5)
#define DIG_CLOCK       0x500E60A0u
#define DIG_RESET       0x500E60C8u
#define DIG_RESET_ON    (1u << 10)
#define DIG_CTRL        0x500DE000u
#define DIG_POWER       ((1u << 5) | (3u << 28))
#define EFUSE_BLK2(w)   (0x5012D05Cu + 4u * (w))
#define EFUSE_BLK10(w)  (0x5012D15Cu + 4u * (w))
#define WAIT_US         1000u
#define CAL_TIMES       10u
#define GPIO_ENABLE_CLR(n) ((n) < 32u ? 0x500E0028u : 0x500E0034u)
#define PIN_MUX(n)      (0x500E1004u + 4u * (n))
#define PIN_ROUTE(n)    (0x500E0558u + 4u * (n))

/* Registers a conversion changes, restored in reverse order after it; the
 * first four are saved before the LP clocks and analog bus are switched on. */
static const uint32_t saved_regs[] = {
    TIKU_P4_LP_CLK, SAR_POWER, TIKU_P4_ANALOG_CONF1, TIKU_P4_ANALOG_CONF2,
    DIG_APB, DIG_CLOCK, DIG_RESET, READER(0), ONESHOT(0), SELECT(0), ATTEN(0),
    READER(1), ONESHOT(1), SELECT(1), ATTEN(1), FORCE_XPD, TSENS};
#define EARLY_REGS 4u
/* SAR analog registers 0..7 and 9: each unit's initial code and reference,
 * the sensor's range, the ground switches, DTEST_VDD_GRP1 and ENT_VDD_GRP1. */
static const uint8_t analog_regs[] = {0u, 1u, 2u, 3u, 4u, 5u, 6u, 7u, 9u};
#define ANALOG_REGS (sizeof(analog_regs) / sizeof(analog_regs[0]))

static uint32_t saved[sizeof(saved_regs) / sizeof(saved_regs[0])];
static uint8_t analog[10];
#define SAVED_WORDS (sizeof(saved) / sizeof(saved[0]) + sizeof(analog))
static uint8_t opened, shift, atten, code_efuse[2], lp_held;
static uint16_t code[2];

/** @brief Change a register field without modifying neighboring fields. */
static void field(uint32_t address, uint32_t mask, uint32_t value)
{
    TIKU_P4_REG_WRITE(address, (TIKU_P4_REG_READ(address) & ~mask) | value);
}

/** @brief Wait a bounded time for the analog bus to finish a transaction. */
static int analog_idle(void)
{
    unsigned i;

    for (i = 0; i < 4096u; i++) {
        if (!(TIKU_P4_REG_READ(TIKU_P4_ANALOG_CTRL) & TIKU_P4_ANALOG_BUSY)) {
            return 0;
        }
    }
    return -1;
}

/** @brief Read SAR analog register @p reg into *value, or write *value to it
 *         and read it back.  @return 0, or -1 on a stuck bus or mismatch */
static int sar_reg(unsigned reg, uint8_t *value, int write)
{
    uint32_t command = 0x69u | (reg << 8);
    uint8_t expected = *value;

    if (analog_idle() != 0) {
        return -1;
    }
    if (write) {
        TIKU_P4_REG_WRITE(TIKU_P4_ANALOG_CTRL, command | TIKU_P4_ANALOG_WRITE |
                                                   ((uint32_t)expected << 16));
        if (analog_idle() != 0) {
            return -1;
        }
    }
    TIKU_P4_REG_WRITE(TIKU_P4_ANALOG_CTRL, command);
    if (analog_idle() != 0) {
        return -1;
    }
    *value = (uint8_t)(TIKU_P4_REG_READ(TIKU_P4_ANALOG_CTRL) >> 16);
    return write && *value != expected ? -1 : 0;
}

/** @brief Write @p value under @p mask into analog register @p reg, keeping
 *         the bits it held before begin(). */
static int sar_set(unsigned reg, uint8_t mask, uint8_t value)
{
    uint8_t byte = (uint8_t)((analog[reg] & ~mask) | (value & mask));
    return sar_reg(reg, &byte, 1);
}

/** @brief Whether the entropy source or another driver holds the digital ADC,
 *  or the analog bus is mid-transaction. */
static int sar_busy(void)
{
    return (TIKU_P4_REG_READ(DIG_APB) & DIG_APB_ON) ||
           (TIKU_P4_REG_READ(DIG_CLOCK) & 1u) || analog_idle() != 0;
}

/** @brief Put back everything begin() changed. */
static void end(unsigned regs_saved)
{
    unsigned i;

    while (regs_saved) {
        --regs_saved;
        (void)sar_reg(analog_regs[regs_saved], &analog[analog_regs[regs_saved]],
                      1);
    }
    if (TIKU_P4_REG_READ(DIG_APB) & DIG_APB_ON) {
        field(DIG_RESET, DIG_RESET_ON, DIG_RESET_ON);
        field(DIG_RESET, DIG_RESET_ON, 0);
    }
    for (i = sizeof(saved) / sizeof(saved[0]); i > 0; i--) {
        TIKU_P4_REG_WRITE(saved_regs[i - 1], saved[i - 1]);
    }
}

/**
 * @brief Take the SAR for one-shot conversions.
 *
 * Clocks the LP ADC, the sensor and the analog bus, powers the bus, saves the
 * analog registers and sets ENT_VDD_GRP1, as adc_ll_regi2c_init() does.
 * @return 0, or -1 with nothing changed if the SAR is held elsewhere
 */
static int begin(void)
{
    unsigned i;

    if (lp_held || sar_busy()) {
        return -1;
    }
    for (i = 0; i < EARLY_REGS; i++) {
        saved[i] = TIKU_P4_REG_READ(saved_regs[i]);
    }
    field(TIKU_P4_LP_CLK, LP_CLOCKS, LP_CLOCKS);
    field(SAR_POWER, 1u << 26, 0);
    tiku_cpu_p4_delay_us(1);
    field(SAR_POWER, 3u << 26, 3u << 26);
    field(TIKU_P4_ANALOG_CONF1, 0xFFFFFFu, 0);
    field(TIKU_P4_ANALOG_CONF2, 0xFFFFFFu, 1u << 7);
    for (; i < sizeof(saved) / sizeof(saved[0]); i++) {
        saved[i] = TIKU_P4_REG_READ(saved_regs[i]);
    }
    for (i = 0; i < ANALOG_REGS; i++) {
        if (sar_reg(analog_regs[i], &analog[analog_regs[i]], 0) != 0) {
            end(0);
            return -1;
        }
    }
    if (sar_set(9u, 0x1Fu, 0x10u) != 0) {
        end(ANALOG_REGS);
        return -1;
    }
    return 0;
}

/**
 * @brief Power and select unit @p unit for RTC-controller conversions.
 *
 * Sets the reference and the initial code; ADC2 also clocks and resets the
 * digital ADC, whose power force it needs.  Runs inside begin()/end().
 */
static int unit_on(unsigned unit, uint16_t value)
{
    unsigned base = unit ? 3u : 0u;

    if (sar_set(base + 2u, 0x70u, 0x40u) != 0 ||
        sar_set(base, 0xFFu, (uint8_t)value) != 0 ||
        sar_set(base + 1u, 0x0Fu, (uint8_t)(value >> 8)) != 0) {
        return -1;
    }
    if (unit) {
        field(DIG_APB, DIG_APB_ON, DIG_APB_ON);
        field(DIG_CLOCK, 1u, 1u);
        field(DIG_RESET, DIG_RESET_ON, DIG_RESET_ON);
        field(DIG_RESET, DIG_RESET_ON, 0);
        field(DIG_CTRL, DIG_POWER, DIG_POWER);
        field(SELECT(1), SELECT_RTC2 | PWDET_CCT, SELECT_RTC2 | (4u << 28));
    } else {
        field(SELECT(0), 1u << 31, 0);
    }
    field(FORCE_XPD, XPD_ON, XPD_ON);
    field(READER(unit), 0xFFu | INVERT(unit), 1u);
    return 0;
}

/** @brief One conversion of pad @p pad (-1: none, for ground) of @p unit at
 *         attenuation @p att.  Runs inside begin()/end(). */
static int convert(unsigned unit, int pad, unsigned att, uint32_t *raw)
{
    unsigned slot = pad < 0 ? (unit ? 2u : 0u) : (unsigned)pad, n;
    uint32_t map = pad < 0 ? 0u : 1u << (PAD_POS + (unsigned)pad);

    field(ATTEN(unit), 3u << (2u * slot), att << (2u * slot));
    field(ONESHOT(unit), 0xFFFu << PAD_POS | START | START_FORCE | PAD_FORCE,
          map | START_FORCE | PAD_FORCE);
    field(ONESHOT(unit), START, START);
    for (n = 0; n < WAIT_US; n++) {
        if (TIKU_P4_REG_READ(ONESHOT(unit)) & DONE) {
            *raw = TIKU_P4_REG_READ(ONESHOT(unit)) & 0xFFFu;
            return 0;
        }
        tiku_cpu_p4_delay_us(1);
    }
    return -1;
}

/**
 * @brief Binary-search the code at which internal ground reads zero.
 *
 * Runs inside begin()/end(): ground switched in, no pad selected.
 */
static int search_code(unsigned unit, uint8_t att, uint16_t *out)
{
    uint32_t list[CAL_TIMES], sum = 0, lo_all, hi_all, raw;
    unsigned rep, i;

    if (unit_on(unit, 2048u) != 0 ||
        sar_set(7u, unit ? 0x80u : 0x20u, 0xFFu) != 0) {
        return -1;
    }
    for (rep = 0; rep < CAL_TIMES; rep++) {
        uint32_t hi = 4096u, lo = 0u, chk = 2048u;
        if (sar_set(unit ? 3u : 0u, 0xFFu, (uint8_t)chk) != 0 ||
            sar_set(unit ? 4u : 1u, 0x0Fu, (uint8_t)(chk >> 8)) != 0 ||
            convert(unit, -1, att, &raw) != 0) {
            return -1;
        }
        while (hi - lo > 1u) {
            if (raw == 0u) {
                hi = chk;
            } else {
                lo = chk;
            }
            chk = (hi + lo) / 2u;
            if (hi - lo == 1u) {
                chk += 1u;
            }
            if (sar_set(unit ? 3u : 0u, 0xFFu, (uint8_t)chk) != 0 ||
                sar_set(unit ? 4u : 1u, 0x0Fu, (uint8_t)(chk >> 8)) != 0 ||
                convert(unit, -1, att, &raw) != 0) {
                return -1;
            }
        }
        list[rep] = chk;
        sum += chk;
    }
    lo_all = hi_all = list[0];
    for (i = 1; i < CAL_TIMES; i++) {
        lo_all = list[i] < lo_all ? list[i] : lo_all;
        hi_all = list[i] > hi_all ? list[i] : hi_all;
    }
    /* Mean of the eight middle codes, rounded up from a remainder of 4. */
    sum -= lo_all + hi_all;
    *out = (uint16_t)(sum / (CAL_TIMES - 2u) +
                      (sum % (CAL_TIMES - 2u) < 4u ? 0u : 1u));
    return 0;
}

/**
 * @brief Read the temperature sensor in its middle range.
 *
 * Runs inside begin()/end(): resets and powers the sensor, waits the 300 us
 * ESP-IDF allows its output to settle, reads it; end() powers it down.
 */
static int read_temp(uint32_t *out)
{
    if (TIKU_P4_REG_READ(TSENS) & TSENS_PU) {
        return -1;
    }
    field(TIKU_P4_LP_RESET, LP_TSENS_RESET, LP_TSENS_RESET);
    field(TIKU_P4_LP_RESET, LP_TSENS_RESET, 0);
    if (sar_set(6u, 0x0Fu, TSENS_MIDDLE) != 0) {
        return -1;
    }
    field(TSENS, TSENS_PU, TSENS_PU);
    tiku_cpu_p4_delay_us(TSENS_SETTLE_US);
    *out = TIKU_P4_REG_READ(TSENS) & 255u;
    return 0;
}

int tiku_p4_adc_ground_code(uint8_t unit, uint8_t att, uint16_t *out)
{
    uint32_t state;
    int rc;

    if (unit > 1u || att > 3u || out == NULL) {
        return -1;
    }
    state = TIKU_P4_IRQ_SAVE();
    if (begin() != 0) {
        TIKU_P4_IRQ_RESTORE(state);
        return -1;
    }
    rc = search_code(unit, att, out);
    end(ANALOG_REGS);
    TIKU_P4_IRQ_RESTORE(state);
    return rc;
}

/**
 * @brief The eFuse initial code of @p unit at @p att, if the chip carries one.
 *
 * Block 2 holds a 10-bit average code per unit and attenuation from bit 128,
 * valid from block version 1; the SAR takes it plus 1400.
 */
static int efuse_code(unsigned unit, uint8_t att, uint16_t *out)
{
    uint32_t word = TIKU_P4_REG_READ(TIKU_P4_EFUSE_BLOCK_VERSION);
    uint32_t version = ((word >> 11) & 3u) * 100u + ((word >> 8) & 7u);
    uint32_t bit = 128u + 40u * unit + 10u * att, value;

    if (version < 1u) {
        return -1;
    }
    value = TIKU_P4_REG_READ(EFUSE_BLK2(bit / 32u)) >> (bit % 32u);
    if (bit % 32u > 22u) {
        value |= TIKU_P4_REG_READ(EFUSE_BLK2(bit / 32u + 1u))
                 << (32u - bit % 32u);
    }
    *out = (uint16_t)((value & 1023u) + 1400u);
    return 0;
}

int tiku_adc_arch_init(const tiku_adc_config_t *config)
{
    static const uint8_t shifts[] = {4u, 2u, 0u};
    static const uint8_t attens[] = {3u, 1u, 2u, 3u};
    uint16_t value[2];
    uint8_t from[2];
    unsigned unit;

    if (config == NULL || config->resolution > TIKU_ADC_RES_12BIT ||
        config->reference > TIKU_ADC_REF_2V5) {
        return TIKU_ADC_ERR_PARAM;
    }
    for (unit = 0; unit < 2u; unit++) {
        if (efuse_code(unit, attens[config->reference], &value[unit]) == 0) {
            from[unit] = 1u;
        } else if (tiku_p4_adc_ground_code((uint8_t)unit,
                                           attens[config->reference],
                                           &value[unit]) == 0) {
            from[unit] = 0u;
        } else {
            return TIKU_ADC_ERR_TIMEOUT;
        }
    }
    for (unit = 0; unit < 2u; unit++) {
        code[unit] = value[unit];
        code_efuse[unit] = from[unit];
    }
    shift = shifts[config->resolution];
    atten = attens[config->reference];
    opened = 1u;
    return TIKU_ADC_OK;
}

void tiku_adc_arch_close(void)
{
    opened = 0u;
}

/** @brief The pad of channel 0..13: GPIO16..23, then GPIO49..54. */
static unsigned pad_of(uint8_t channel)
{
    return channel < 8u ? 16u + channel : 41u + channel;
}

int tiku_adc_arch_channel_init(uint8_t channel)
{
    unsigned pad = pad_of(channel);
    uint32_t state;

    if (channel == TIKU_ADC_CH_TEMP) {
        return TIKU_ADC_OK;
    }
    if (channel > 13u ||
        tiku_gpio_arch_is_peripheral((uint8_t)(pad / 8u + 1u),
                                     (uint8_t)(pad % 8u)) != 0 ||
        tiku_gpio_owner((uint8_t)(pad / 8u + 1u), (uint8_t)(pad % 8u)) !=
            NULL) {
        return TIKU_ADC_ERR_PARAM;
    }
    state = TIKU_P4_IRQ_SAVE();
    TIKU_P4_REG_WRITE(GPIO_ENABLE_CLR(pad), 1u << (pad % 32u));
    TIKU_P4_REG_WRITE(PIN_ROUTE(pad), 256u);
    /* GPIO function; input, pull-up and pull-down off. */
    field(PIN_MUX(pad), (7u << 12) | (7u << 7), 1u << 12);
    TIKU_P4_IRQ_RESTORE(state);
    return TIKU_ADC_OK;
}

int tiku_adc_arch_read(uint8_t channel, uint16_t *value)
{
    unsigned unit = channel >= 8u;
    uint32_t state, raw = 0;
    int rc = TIKU_ADC_OK;

    if (!opened || (channel > 13u && channel != TIKU_ADC_CH_TEMP) ||
        value == NULL) {
        return TIKU_ADC_ERR_PARAM;
    }
    state = TIKU_P4_IRQ_SAVE();
    if (begin() != 0) {
        TIKU_P4_IRQ_RESTORE(state);
        return TIKU_ADC_ERR_TIMEOUT;
    }
    if (channel == TIKU_ADC_CH_TEMP) {
        if (read_temp(&raw) != 0) {
            rc = TIKU_ADC_ERR_TIMEOUT;
        }
    } else if (unit_on(unit, code[unit]) != 0 ||
               convert(unit, (int)(unit ? channel - 8u + 2u : channel), atten,
                       &raw) != 0) {
        rc = TIKU_ADC_ERR_TIMEOUT;
    } else {
        raw >>= shift;
    }
    end(ANALOG_REGS);
    TIKU_P4_IRQ_RESTORE(state);
    if (rc == TIKU_ADC_OK) {
        *value = (uint16_t)raw;
    }
    return rc;
}

int tiku_p4_adc_celsius(uint16_t value)
{
    uint32_t cal = TIKU_P4_REG_READ(EFUSE_BLK10(3)) & 511u;
    int tenths = (cal & 256u) ? -(int)(cal & 255u) : (int)(cal & 255u);

    return ((int)value * 4386 - 205200 - tenths * 1000) / 10000;
}

uint16_t tiku_p4_adc_init_code(uint8_t unit, int *from_efuse)
{
    if (unit > 1u) {
        return 0u;
    }
    if (from_efuse != NULL) {
        *from_efuse = code_efuse[unit];
    }
    return code[unit];
}

#if TIKU_LPCORE_ENABLE
_Static_assert(SAVED_WORDS <= TIKU_LPCORE_SAR_WORDS,
               "adc: the saved state outgrew the LP SRAM keep");

/** @brief What a release puts back: the state an earlier boot's hold kept in
 *         LP SRAM, or else this hold's, kept there for a later boot. */
static void keep_saved(void)
{
    uint32_t words[SAVED_WORDS];
    unsigned i, n = sizeof(saved) / sizeof(saved[0]);

    if (tiku_p4_lpcore_sar_kept(words, SAVED_WORDS)) {
        for (i = 0u; i < n; i++) {
            saved[i] = words[i];
        }
        for (i = 0u; i < sizeof(analog); i++) {
            analog[i] = (uint8_t)words[n + i];
        }
        return;
    }
    for (i = 0u; i < n; i++) {
        words[i] = saved[i];
    }
    for (i = 0u; i < sizeof(analog); i++) {
        words[n + i] = analog[i];
    }
    (void)tiku_p4_lpcore_sar_keep(words, SAVED_WORDS);
}
#define KEEP_SAVED()  keep_saved()
#define KEEP_FORGET() tiku_p4_lpcore_sar_forget()
#else
#define KEEP_SAVED()  ((void)0)
#define KEEP_FORGET() ((void)0)
#endif

int tiku_p4_adc_lp_hold(void)
{
    uint16_t value;
    uint32_t state;
    unsigned pad;
    int rc = -1;

    if (lp_held) {
        return 0;
    }
    SAR_TAKE();
    if (efuse_code(0u, 3u, &value) == 0 ||
        tiku_p4_adc_ground_code(0u, 3u, &value) == 0) {
        state = TIKU_P4_IRQ_SAVE();
        if (begin() == 0) {
            if (unit_on(0u, value) == 0) {
                for (pad = 0u; pad < 8u; pad++) {
                    field(ATTEN(0), 3u << (2u * pad), 3u << (2u * pad));
                }
                lp_held = 1u;
                rc = 0;
                KEEP_SAVED();
            } else {
                end(ANALOG_REGS);
            }
        }
        TIKU_P4_IRQ_RESTORE(state);
    }
    SAR_GIVE();
    return rc;
}

void tiku_p4_adc_lp_release(void)
{
    uint32_t state;

    if (!lp_held) {
        return;
    }
    SAR_TAKE();
    state = TIKU_P4_IRQ_SAVE();
    end(ANALOG_REGS);
    lp_held = 0u;
    KEEP_FORGET();
    TIKU_P4_IRQ_RESTORE(state);
    SAR_GIVE();
}
