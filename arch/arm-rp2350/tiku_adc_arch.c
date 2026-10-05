/*
 * Tiku Operating System v0.06
 * Simple. Ubiquitous. Intelligence, Everywhere.
 * http://tiku-os.org
 *
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 *
 * tiku_adc_arch.c - RP2350 ADC driver.
 *
 * One 12-bit SAR ADC over AIN0-AIN3 plus the internal temperature sensor.  The
 * reference is fixed to ADC_AVDD, so the reference in tiku_adc_config_t is
 * ignored; a narrower requested resolution is shifted down from 12 bits.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include "tiku_adc_arch.h"
#include "tiku_rp2350_regs.h"
#include <stdint.h>

/**
 * @brief Right-shift applied to the 12-bit raw ADC result.
 *
 * 0 = 12-bit (no shift), 2 = 10-bit, 4 = 8-bit.
 * Set once during tiku_adc_arch_init() from config->resolution.
 */
static uint8_t adc_result_shift;

/**
 * @brief Non-zero after a successful tiku_adc_arch_init().
 *
 * tiku_adc_arch_read() returns TIKU_ADC_ERR_PARAM while it is 0, and
 * tiku_adc_arch_close() clears it.
 */
static uint8_t adc_initialised;

/**
 * @brief Map a kernel channel ID to the RP2350 AINSEL selector value.
 *
 * Channels 0..3 become AINSEL 0..3 (GPIO26..GPIO29), channel 30
 * (TIKU_ADC_CH_TEMP) becomes 4, and channel 31 (TIKU_ADC_CH_BATTERY) becomes
 * 3, the VSYS/3 divider on GP29.
 *
 * @param channel  Kernel ADC channel ID (0..3, 30, or 31)
 * @return AINSEL value (0..4), or 0xFF for unsupported channels
 */
static uint8_t map_channel(uint8_t channel) {
    if (channel <= 3U) {
        return channel;                          /* AIN0..AIN3 */
    }
    if (channel == 30U /* TIKU_ADC_CH_TEMP */) {
        return RP2350_ADC_CHANNEL_TEMP;
    }
    if (channel == 31U /* TIKU_ADC_CH_BATTERY */) {
        return 3U;                               /* GP29 (VSYS / 3) */
    }
    return 0xFFU;
}

/**
 * @brief Initialise the RP2350 ADC peripheral.
 *
 * Decodes the requested resolution into a result-shift amount, brings the ADC
 * out of reset, points clk_adc at the 12 MHz XOSC, and waits for READY with a
 * bounded spin.  The reference in @p config is ignored: it is fixed to AVDD.
 *
 * @param config  ADC configuration (resolution, reference); must be non-NULL.
 * @return TIKU_ADC_OK on success, TIKU_ADC_ERR_PARAM for a NULL config
 *         or unrecognised resolution, TIKU_ADC_ERR_TIMEOUT if the READY
 *         bit does not assert within ~100 000 iterations.
 */
int tiku_adc_arch_init(const tiku_adc_config_t *config) {
    if (config == (const tiku_adc_config_t *)0) {
        return TIKU_ADC_ERR_PARAM;
    }

    /* Resolution -> result shift. RP2350 hardware is always 12-bit. */
    switch (config->resolution) {
    case TIKU_ADC_RES_8BIT:  adc_result_shift = 4U; break;
    case TIKU_ADC_RES_10BIT: adc_result_shift = 2U; break;
    case TIKU_ADC_RES_12BIT: adc_result_shift = 0U; break;
    default:
        return TIKU_ADC_ERR_PARAM;
    }

    /* The reference is fixed to ADC_AVDD; config->reference has no effect. */
    (void)config->reference;

    /* Bring the ADC out of reset. */
    rp2350_unreset(RP2350_RESETS_ADC);

    /* Run clk_adc from XOSC (12 MHz), which is always running; the boot
     * path does not start PLL_USB.  DIV resets to 1 (no divide).  With
     * clk_adc gated, READY never asserts.
     *
     * The generator is disabled while AUXSRC changes, so the switch does
     * not glitch, then enabled again. */
    _RP2350_REG(RP2350_CLK_ADC_CTRL) = 0U;
    _RP2350_REG(RP2350_CLK_ADC_CTRL) =
        RP2350_CLK_ADC_AUXSRC_XOSC | RP2350_CLK_ADC_ENABLE;

    /* Enable the ADC; free-running mode and the temperature sensor stay off. */
    _RP2350_REG(RP2350_ADC_CS) = RP2350_ADC_CS_EN;

    /* READY rises once the ADC is idle.  The spin is bounded: with no
     * clock, init returns TIKU_ADC_ERR_TIMEOUT. */
    {
        uint32_t spin;
        for (spin = 0U; spin < 100000U; spin++) {
            if (_RP2350_REG(RP2350_ADC_CS) & RP2350_ADC_CS_READY) {
                break;
            }
        }
        if ((_RP2350_REG(RP2350_ADC_CS) & RP2350_ADC_CS_READY) == 0U) {
            return TIKU_ADC_ERR_TIMEOUT;
        }
    }

    adc_initialised = 1U;
    return TIKU_ADC_OK;
}

/**
 * @brief Disable the RP2350 ADC peripheral.
 *
 * Writes 0 to CS, which clears EN and TS_EN: conversions stop and the
 * temperature sensor powers down.  The ADC stays out of reset and clk_adc
 * stays on.
 */
void tiku_adc_arch_close(void) {
    _RP2350_REG(RP2350_ADC_CS) = 0U;
    adc_initialised = 0U;
}

/**
 * @brief Configure the GPIO pin for an ADC channel.
 *
 * For external channels 0..3 and the battery channel, programmes the matching
 * GPIO (GPIO26..GPIO29) as a high-impedance analog input by setting
 * output-disable and clearing all pulls.  The temperature channel needs none.
 *
 * @param channel  Kernel ADC channel ID (0..3, 30, or 31)
 * @return TIKU_ADC_OK on success, TIKU_ADC_ERR_PARAM for unsupported channels
 */
int tiku_adc_arch_channel_init(uint8_t channel) {
    /* Only external pins need GPIO config; read() powers the temperature
     * sensor when it selects channel 30. */
    if (channel <= 3U) {
        uint8_t pin = (uint8_t)(RP2350_ADC_GPIO_BASE + channel);
        /* High-impedance analog input: input buffer off, pulls off,
         * output disabled.  A pull or an enabled buffer loads the pin. */
        _RP2350_REG(RP2350_PADS_BANK0_GPIO(pin)) = RP2350_PADS_OD;
        return TIKU_ADC_OK;
    }
    if (channel == 31U /* battery */) {
        uint8_t pin = (uint8_t)(RP2350_ADC_GPIO_BASE + 3U);
        _RP2350_REG(RP2350_PADS_BANK0_GPIO(pin)) = RP2350_PADS_OD;
        return TIKU_ADC_OK;
    }
    if (channel == 30U /* temp */) {
        return TIKU_ADC_OK;
    }
    return TIKU_ADC_ERR_PARAM;
}

/**
 * @brief Trigger a single ADC conversion and return the result.
 *
 * Selects the channel via AINSEL, enables the temperature-sensor bias when
 * needed, clears any sticky error, fires START_ONCE and waits for READY with a
 * bounded spin.  The 12-bit raw result is shifted to the configured resolution.
 *
 * @param channel  Kernel ADC channel ID (0..3, 30, or 31)
 * @param value    Output pointer for the conversion result; must be non-NULL
 * @return TIKU_ADC_OK on success, TIKU_ADC_ERR_PARAM for NULL value pointer,
 *         uninitialised ADC, or unsupported channel,
 *         TIKU_ADC_ERR_TIMEOUT if READY does not assert or ERR is set
 */
int tiku_adc_arch_read(uint8_t channel, uint16_t *value) {
    if (value == (uint16_t *)0 || adc_initialised == 0U) {
        return TIKU_ADC_ERR_PARAM;
    }

    uint8_t ainsel = map_channel(channel);
    if (ainsel == 0xFFU) {
        return TIKU_ADC_ERR_PARAM;
    }

    /* CS keeps EN, sets TS_EN only for the temperature channel, writes 1
     * to ERR_STICKY, which clears it, and selects AINSEL. */
    uint32_t cs = RP2350_ADC_CS_EN | RP2350_ADC_CS_ERR_STICKY;
    if (ainsel == RP2350_ADC_CHANNEL_TEMP) {
        cs |= RP2350_ADC_CS_TS_EN;
    }
    cs |= ((uint32_t)ainsel << RP2350_ADC_CS_AINSEL_SHIFT) &
          RP2350_ADC_CS_AINSEL_MASK;
    _RP2350_REG(RP2350_ADC_CS) = cs;

    /* Trigger one conversion. START_ONCE is one-shot -- the bit reads
     * back as 0 once accepted; the conversion runs asynchronously. */
    _RP2350_REG(RP2350_ADC_CS) = cs | RP2350_ADC_CS_START_ONCE;

    /* READY rises when the conversion completes.  The spin is bounded: a
     * stalled ADC returns TIKU_ADC_ERR_TIMEOUT. */
    {
        uint32_t spin;
        for (spin = 0U; spin < 100000U; spin++) {
            if (_RP2350_REG(RP2350_ADC_CS) & RP2350_ADC_CS_READY) {
                break;
            }
        }
        if ((_RP2350_REG(RP2350_ADC_CS) & RP2350_ADC_CS_READY) == 0U) {
            return TIKU_ADC_ERR_TIMEOUT;
        }
    }

    if (_RP2350_REG(RP2350_ADC_CS) & RP2350_ADC_CS_ERR) {
        return TIKU_ADC_ERR_TIMEOUT;
    }

    uint32_t raw = _RP2350_REG(RP2350_ADC_RESULT) & 0xFFFU;   /* 12-bit */
    *value = (uint16_t)(raw >> adc_result_shift);
    return TIKU_ADC_OK;
}
