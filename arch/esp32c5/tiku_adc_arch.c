/*
 * Tiku Operating System v0.06
 * Simple. Persistent. Intelligent.
 * http://tiku-os.org
 *
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 *
 * tiku_adc_arch.c - C5 SAR ADC1 one-shot conversions, the temperature sensor.
 *
 * Register sequence: ESP-IDF 4d59230 adc_ll.h, adc_oneshot_hal.c,
 * adc_hal_common.c, sar_periph_ctrl.c, temperature_sensor_ll.h,
 * temperature_sensor_hal.c, regi2c_saradc.h and the C5 eFuse table.
 *
 * SPDX-License-Identifier: Apache-2.0
 */
#include <stddef.h>
#include "tiku_adc_arch.h"
#include "tiku_esp32c5_regs.h"
#include "tiku_irq_arch.h"
#include "tiku_cpu_common.h"
#include "tiku_analog_arch.h"
#include <interfaces/gpio/tiku_gpio_owner.h>

#define SAR             0x6000E000u
#define SAR_CTRL        SAR
#define SAR_CTRL2       (SAR + 0x04u)
#define SAR_ONETIME     (SAR + 0x20u)
#define SAR_DATA        (SAR + 0x2Cu)
#define SAR_INT_RAW     (SAR + 0x44u)
#define SAR_INT_CLR     (SAR + 0x4Cu)
#define SAR_TSENS       (SAR + 0x58u)
#define PCR_SAR_CONF    0x60096088u
#define PCR_SAR_CLKM    0x6009608Cu
#define PCR_SAR_DIV     0x60096148u
#define PCR_TSENS       0x60096090u
#define FE_CLOCK        0x600A9C14u
#define ANA_CLOCK       0x600AF018u
#define ANA_SELECT      0x600AF81Cu
#define RF_PWC          0x600B0158u
#define RNG_RESET       0x600B2804u
#define RNG_CFG         0x600B2824u
#define EFUSE_BLK1(w)   (0x600B4844u + 4u * (w))
#define EFUSE_BLK2(w)   (0x600B485Cu + 4u * (w))
#define ONETIME_START   (1u << 29)
#define ONETIME_SAR1    (1u << 31)
#define ONETIME_NONE    (15u << 25)
#define ADC1_DONE       (1u << 31)
#define SAR_FORCE_ON    ((1u << 6) | (3u << 27))
#define FE_CLOCKS       ((1u << 13) | (1u << 15))
#define TSENS_PU        (1u << 22)
#define TSENS_CLOCK     ((1u << 22) | (1u << 20))
#define TSENS_SETTLE_US 300u
#define TSENS_MIDDLE    15u
#define WAIT_US         1000u
#define CAL_TIMES       10u
#define PIN_MUX(n)      (0x60090000u + 4u * (n))
#define PIN_ROUTE(n)    (0x60091AD4u + 4u * (n))
#define PIN_ENABLE      0x60091034u

/* Registers a conversion changes, restored in reverse order after it. */
static const uint32_t saved_regs[] = {
    PCR_SAR_CONF, PCR_SAR_CLKM, PCR_SAR_DIV, RF_PWC,  ANA_CLOCK,   FE_CLOCK,
    ANA_SELECT,   PCR_TSENS,    SAR_TSENS,   SAR_CTRL, SAR_ONETIME};
/* SAR analog registers: initial code low and high, reference, the
 * temperature sensor's range, ground. */
static const uint8_t analog_regs[] = {0u, 1u, 2u, 6u, 8u};

/* The temperature sensor's ranges (its range register value and offset),
 * from temperature_sensor_periph.c; the middle one is offset 0. */
static const struct {
    uint8_t reg;
    int8_t offset;
} ranges[] = {{5u, -2}, {7u, -1}, {15u, 0}, {11u, 1}, {10u, 2}};

static uint32_t saved[sizeof(saved_regs) / sizeof(saved_regs[0])];
static uint8_t analog[sizeof(analog_regs)];
static uint8_t opened, shift, atten, code_efuse;
/* 1 while a conversion borrows the SAR from the radio, which keeps it
 * powered and clocked. */
static uint8_t shared;
static uint16_t code;

/** @brief Change a register field without modifying neighboring fields. */
static void field(uint32_t address, uint32_t mask, uint32_t value)
{
    TIKU_C5_REG_WRITE(address, (TIKU_C5_REG_READ(address) & ~mask) | value);
}

/** @brief Whether an ADC conversion, the temperature sensor or the RNG's
 *  sampling is running on the SAR; the radio keeps the sensor powered. */
static int sar_busy(void)
{
    return (TIKU_C5_REG_READ(SAR_CTRL) & 2u) ||
           (!shared && (TIKU_C5_REG_READ(SAR_TSENS) & TSENS_PU)) ||
           (TIKU_C5_REG_READ(SAR_CTRL2) & (1u << 24)) ||
           (TIKU_C5_REG_READ(SAR_ONETIME) & ONETIME_START) ||
           (TIKU_C5_REG_READ(RNG_CFG) & 1u) ||
           (TIKU_C5_REG_READ(RNG_RESET) & (1u << 24));
}

/** @brief Put back everything begin() changed and release the SAR. */
static void end(unsigned regs_saved)
{
    unsigned i;

    TIKU_C5_REG_WRITE(SAR_ONETIME, ONETIME_NONE);
    while (regs_saved) {
        --regs_saved;
        (void)tiku_c5_analog_sar_reg(analog_regs[regs_saved],
                                     &analog[regs_saved], 1);
    }
    for (i = sizeof(saved) / sizeof(saved[0]); i > 0; i--) {
        TIKU_C5_REG_WRITE(saved_regs[i - 1], saved[i - 1]);
    }
    if (!shared) {
        (void)tiku_c5_analog_release(TIKU_C5_ANALOG_ADC);
    }
}

/**
 * @brief Take the SAR, or borrow it from the radio, for one-shot conversions.
 *
 * A free SAR is clocked from the crystal / 16 with its analog bus and front
 * end woken; the radio's keeps its power and divider.  Both save the analog
 * registers, clock the controller and set the reference.
 * @return 0, or -1 with nothing changed if the SAR is held elsewhere
 */
static int begin(void)
{
    unsigned i, owner = tiku_c5_analog_owner();
    uint8_t dref;

    shared = owner == TIKU_C5_ANALOG_PHY;
    if ((owner != TIKU_C5_ANALOG_NONE && !shared) || sar_busy() ||
        (!shared && tiku_c5_analog_acquire(TIKU_C5_ANALOG_ADC) != 0)) {
        return -1;
    }
    for (i = 0; i < sizeof(saved) / sizeof(saved[0]); i++) {
        saved[i] = TIKU_C5_REG_READ(saved_regs[i]);
    }
    /* Beside the radio a conversion changes only the controller's clock gates
     * (BLE stops its function clock), the initial code, the reference and the
     * one-shot controls, as ESP-IDF's one-shot read does. */
    if (shared) {
        field(PCR_SAR_CONF, 4u, 4u);
        field(PCR_SAR_CLKM, 1u << 22, 1u << 22);
    } else {
        field(PCR_SAR_CONF, 4u, 4u);
        field(PCR_SAR_CLKM, 0x7FFFFFu, (1u << 22) | (15u << 12));
        field(PCR_SAR_DIV, 255u << 8, 1u << 8);
        field(RF_PWC, 1u << 26, 0);
        tiku_cpu_c5_delay_us(1);
        field(RF_PWC, 3u << 26, 3u << 26);
        field(ANA_CLOCK, 4u, 4u);
        field(FE_CLOCK, FE_CLOCKS, FE_CLOCKS);
    }
    for (i = 0; i < sizeof(analog); i++) {
        if (tiku_c5_analog_sar_reg(analog_regs[i], &analog[i], 0) != 0) {
            end(0);
            return -1;
        }
    }
    dref = (uint8_t)((analog[2] & ~0x70u) | 0x10u);
    if (tiku_c5_analog_sar_reg(2u, &dref, 1) != 0) {
        end(sizeof(analog));
        return -1;
    }
    field(SAR_CTRL, SAR_FORCE_ON, SAR_FORCE_ON);
    return 0;
}

/** @brief Write the 12-bit initial code into the SAR. */
static int set_code(uint16_t value)
{
    uint8_t low = (uint8_t)value;
    uint8_t high = (uint8_t)((analog[1] & 0xF0u) | ((value >> 8) & 0x0Fu));

    return tiku_c5_analog_sar_reg(0u, &low, 1) ||
           tiku_c5_analog_sar_reg(1u, &high, 1) ? -1 : 0;
}

/** @brief One conversion with @p select in the one-shot register. */
static int convert(uint32_t select, uint32_t *raw)
{
    unsigned n;

    TIKU_C5_REG_WRITE(SAR_ONETIME, select);
    TIKU_C5_REG_WRITE(SAR_INT_CLR, ADC1_DONE);
    /* The start edge must last three of the controller's 3 MHz clocks. */
    TIKU_C5_REG_WRITE(SAR_ONETIME, select | ONETIME_START);
    tiku_cpu_c5_delay_us(3);
    TIKU_C5_REG_WRITE(SAR_ONETIME, select);
    tiku_cpu_c5_delay_us(3);
    for (n = 0; n < WAIT_US; n++) {
        if (TIKU_C5_REG_READ(SAR_INT_RAW) & ADC1_DONE) {
            *raw = TIKU_C5_REG_READ(SAR_DATA) & 0xFFFu;
            return 0;
        }
        tiku_cpu_c5_delay_us(1);
    }
    return -1;
}

/**
 * @brief Binary-search the code at which internal ground reads zero.
 *
 * Runs inside begin()/end(): ground switched in, no channel selected.
 */
static int search_code(uint8_t att, uint16_t *out)
{
    uint32_t select = ((uint32_t)att << 23) | ONETIME_NONE | ONETIME_SAR1;
    uint32_t list[CAL_TIMES], sum = 0, lo_all, hi_all, raw;
    uint8_t gnd = (uint8_t)(analog[4] | 2u);
    unsigned rep, i;

    if (tiku_c5_analog_sar_reg(8u, &gnd, 1) != 0) {
        return -1;
    }
    for (rep = 0; rep < CAL_TIMES; rep++) {
        uint32_t hi = 4096u, lo = 0u, chk = 2048u;
        if (set_code((uint16_t)chk) || convert(select, &raw)) {
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
            if (set_code((uint16_t)chk) || convert(select, &raw)) {
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
 * @brief Read the temperature sensor on the middle range's scale.
 *
 * Runs inside begin()/end().  A powered-down sensor is powered in its middle
 * range; one the radio keeps on is read in its range and moved onto the
 * middle range's scale, 63.57 codes per range step (the HAL's factors).
 */
static int read_temp(uint32_t *out)
{
    uint8_t reg = (uint8_t)((analog[3] & 0xF0u) | TSENS_MIDDLE);
    int offset = 0, value;
    unsigned i;

    if (TIKU_C5_REG_READ(SAR_TSENS) & TSENS_PU) {
        for (i = 0; i < sizeof(ranges) / sizeof(ranges[0]); i++) {
            if ((analog[3] & 0x0Fu) == ranges[i].reg) {
                break;
            }
        }
        if (i == sizeof(ranges) / sizeof(ranges[0])) {
            return -1;
        }
        offset = ranges[i].offset;
    } else {
        field(PCR_TSENS, TSENS_CLOCK, TSENS_CLOCK);
        if (tiku_c5_analog_sar_reg(6u, &reg, 1) != 0) {
            return -1;
        }
        field(SAR_TSENS, TSENS_PU, TSENS_PU);
        tiku_cpu_c5_delay_us(TSENS_SETTLE_US);
    }
    value = (int)(TIKU_C5_REG_READ(SAR_TSENS) & 255u) * 4386 - offset * 278800;
    *out = value > 0 ? (uint32_t)((value + 2193) / 4386) : 0u;
    return 0;
}

int tiku_c5_adc_ground_code(uint8_t att, uint16_t *out)
{
    uint32_t state;
    int rc;

    if (att > 3u || out == NULL) {
        return -1;
    }
    state = TIKU_C5_IRQ_SAVE();
    if (begin() != 0) {
        TIKU_C5_IRQ_RESTORE(state);
        return -1;
    }
    rc = search_code(att, out);
    end(sizeof(analog));
    TIKU_C5_IRQ_RESTORE(state);
    return rc;
}

/**
 * @brief The eFuse initial code for @p att, if the chip carries one.
 *
 * Block 2 holds a 10-bit average code per attenuation from bit 145, valid
 * for block versions 1..99; the SAR takes it plus 1600.
 */
static int efuse_code(uint8_t att, uint16_t *out)
{
    uint32_t word = TIKU_C5_REG_READ(EFUSE_BLK1(2));
    uint32_t version = ((word >> 11) & 3u) * 100u + ((word >> 8) & 7u);
    uint32_t bit = 145u + 10u * att, value;

    if (version < 1u || version > 99u) {
        return -1;
    }
    value = TIKU_C5_REG_READ(EFUSE_BLK2(bit / 32u)) >> (bit % 32u);
    if (bit % 32u > 22u) {
        value |= TIKU_C5_REG_READ(EFUSE_BLK2(bit / 32u + 1u))
                 << (32u - bit % 32u);
    }
    *out = (uint16_t)((value & 1023u) + 1600u);
    return 0;
}

int tiku_adc_arch_init(const tiku_adc_config_t *config)
{
    static const uint8_t shifts[] = {4u, 2u, 0u};
    static const uint8_t attens[] = {3u, 1u, 2u, 3u};
    uint16_t value;

    if (config == NULL || config->resolution > TIKU_ADC_RES_12BIT ||
        config->reference > TIKU_ADC_REF_2V5) {
        return TIKU_ADC_ERR_PARAM;
    }
    if (efuse_code(attens[config->reference], &value) == 0) {
        code_efuse = 1u;
    } else if (tiku_c5_adc_ground_code(attens[config->reference], &value) ==
               0) {
        code_efuse = 0u;
    } else {
        return TIKU_ADC_ERR_TIMEOUT;
    }
    shift = shifts[config->resolution];
    atten = attens[config->reference];
    code = value;
    opened = 1u;
    return TIKU_ADC_OK;
}

void tiku_adc_arch_close(void)
{
    opened = 0u;
}

int tiku_adc_arch_channel_init(uint8_t channel)
{
    unsigned pad = channel + 1u;
    uint32_t state;

    if (channel == TIKU_ADC_CH_TEMP) {
        return TIKU_ADC_OK;
    }
    if (channel > 5u || tiku_gpio_owner(pad / 8u + 1u, pad % 8u) != NULL) {
        return TIKU_ADC_ERR_PARAM;
    }
    state = TIKU_C5_IRQ_SAVE();
    TIKU_C5_REG_WRITE(PIN_ENABLE + 8u, 1u << pad);
    TIKU_C5_REG_WRITE(PIN_ROUTE(pad), 256u);
    /* GPIO function; input, pull-up and pull-down off. */
    field(PIN_MUX(pad), (7u << 12) | (7u << 7), 1u << 12);
    TIKU_C5_IRQ_RESTORE(state);
    return TIKU_ADC_OK;
}

int tiku_adc_arch_read(uint8_t channel, uint16_t *value)
{
    uint32_t state, raw = 0;
    int rc = TIKU_ADC_OK;

    if (!opened || (channel > 5u && channel != TIKU_ADC_CH_TEMP) ||
        value == NULL) {
        return TIKU_ADC_ERR_PARAM;
    }
    state = TIKU_C5_IRQ_SAVE();
    if (begin() != 0) {
        TIKU_C5_IRQ_RESTORE(state);
        return TIKU_ADC_ERR_TIMEOUT;
    }
    if (channel == TIKU_ADC_CH_TEMP) {
        if (read_temp(&raw) != 0) {
            rc = TIKU_ADC_ERR_TIMEOUT;
        }
    } else if (set_code(code) != 0 ||
               convert(((uint32_t)atten << 23) | ((uint32_t)channel << 25) |
                           ONETIME_SAR1, &raw) != 0) {
        rc = TIKU_ADC_ERR_TIMEOUT;
    } else {
        raw >>= shift;
    }
    end(sizeof(analog));
    TIKU_C5_IRQ_RESTORE(state);
    if (rc == TIKU_ADC_OK) {
        *value = (uint16_t)raw;
    }
    return rc;
}

int tiku_c5_adc_celsius(uint16_t value)
{
    uint32_t cal = TIKU_C5_REG_READ(EFUSE_BLK2(4)) & 511u;
    int tenths = (cal & 256u) ? -(int)(cal & 255u) : (int)(cal & 255u);

    return ((int)value * 4386 - 205200 - tenths * 1000) / 10000;
}

uint16_t tiku_c5_adc_init_code(int *from_efuse)
{
    if (from_efuse != NULL) {
        *from_efuse = code_efuse;
    }
    return code;
}
