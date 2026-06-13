/*
 * Tiku Operating System v0.05
 * Simple. Ubiquitous. Intelligence, Everywhere.
 * http://tiku-os.org
 *
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 *
 * tiku_adc_arch.c - RP2350 ADC driver
 *
 * Single 12-bit SAR ADC. Channels:
 *   0..3  external pins AIN0..AIN3 (GPIO26..GPIO29)
 *   4     internal temperature sensor (requires CS.TS_EN)
 *
 * The kernel's ADC interface uses MSP430-style channel constants:
 *   TIKU_ADC_CH_TEMP    (30)  -> AINSEL=4 + TS_EN
 *   TIKU_ADC_CH_BATTERY (31)  -> AINSEL=3 (GP29 is wired to VSYS via a
 *                                          1:3 divider on the standard
 *                                          Pico 2 board; Pico 2 W shares
 *                                          the same pin so the reading
 *                                          is meaningful only when the
 *                                          CYW43 isn't sampling)
 *
 * Reference is fixed to ADC_AVDD (~3.3 V on the LaunchPad-style boards).
 * The MSP430 internal-reference selectors (TIKU_ADC_REF_1V2 / _2V0 /
 * _2V5) are accepted but have no effect — the reference is whatever is
 * on the ADC_AVDD pin. Resolution is fixed at 12 bits in hardware; if
 * the caller asked for 8 or 10 bits we right-shift the result so the
 * value field carries the requested precision.
 *
 * Clock setup: the ADC needs clk_adc to actually count. PLL_USB isn't
 * brought up by the boot sequence today, so we point clk_adc at XOSC
 * (12 MHz) here in the driver. That's well below the spec'd 48 MHz
 * but the ADC is happy with it; conversions just take longer. When a
 * real clk_adc is wired into tiku_cpu_freq_boot_arch.c we can drop
 * the local clock setup.
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
 * @brief Non-zero when the ADC has been successfully initialised.
 *
 * Guards tiku_adc_arch_read() so reads before init return an error
 * instead of accessing uninitialised hardware registers.
 */
static uint8_t adc_initialised;

/**
 * @brief Map a kernel channel ID to the RP2350 AINSEL selector value.
 *
 * Translates the MSP430-style channel constants used by the kernel ADC
 * interface to the 3-bit AINSEL field written to the ADC CS register:
 *   - channels 0..3  -> AINSEL 0..3 (external AIN0..AIN3 / GPIO26..GPIO29)
 *   - channel 30 (TIKU_ADC_CH_TEMP)    -> AINSEL 4 (internal temp sensor)
 *   - channel 31 (TIKU_ADC_CH_BATTERY) -> AINSEL 3 (GP29 / VSYS / 3 divider)
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
 * Decodes the requested resolution into a result-shift amount, brings
 * the ADC block out of reset, points clk_adc at the 12 MHz XOSC (the
 * PLL_USB source is not available yet at the point this is called), and
 * waits for the READY bit with a bounded spin. The reference voltage is
 * hardware-fixed to ADC_AVDD; any reference selector in @p config is
 * accepted but silently ignored.
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

    /* Reference is hardware-fixed to ADC_AVDD. Other settings are
     * accepted (so the API contract holds) but have no effect. */
    (void)config->reference;

    /* Bring the ADC out of reset. */
    rp2350_unreset(RP2350_RESETS_ADC);

    /* Point clk_adc at XOSC (12 MHz). The boot path doesn't bring up
     * PLL_USB, so PLL_USB-as-clk_adc isn't an option today; XOSC is
     * always available. DIV reset value is 1 (no divide). Without
     * this step clk_adc is gated and the ADC's READY bit never
     * asserts -- causing init to spin forever (the previous failure
     * mode that hung init-shell-cmds).
     *
     * Disable first to gate the glitch-free mux, then re-enable. */
    _RP2350_REG(RP2350_CLK_ADC_CTRL) = 0U;
    _RP2350_REG(RP2350_CLK_ADC_CTRL) =
        RP2350_CLK_ADC_AUXSRC_XOSC | RP2350_CLK_ADC_ENABLE;

    /* Clear sticky error, leave free-running off, no IRQs. Enable. */
    _RP2350_REG(RP2350_ADC_CS) = RP2350_ADC_CS_EN;

    /* Wait for the ADC to settle (READY bit goes high when idle).
     * Bounded so a missing clock surfaces as a clean ERR_TIMEOUT
     * instead of a kernel hang. */
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
 * Clears the CS.EN bit to stop conversions. The ADC is NOT put back
 * into reset so that the temperature-sensor bias is preserved across
 * close/re-init cycles. The disabled ADC draws negligible current.
 */
void tiku_adc_arch_close(void) {
    /* Disable the ADC. We do not put it back in reset -- that would
     * also drop the temperature-sensor bias and cost a longer warm-up
     * on the next init. The disabled ADC draws negligible current. */
    _RP2350_REG(RP2350_ADC_CS) = 0U;
    adc_initialised = 0U;
}

/**
 * @brief Configure the GPIO pin for an ADC channel.
 *
 * For external channels 0..3 and the battery channel (31), programmes the
 * matching GPIO (GPIO26..GPIO29) as a high-impedance analog input by setting
 * the output-disable bit and clearing all pull resistors. The internal
 * temperature channel (30) requires no GPIO configuration and returns
 * TIKU_ADC_OK immediately.
 *
 * @param channel  Kernel ADC channel ID (0..3, 30, or 31)
 * @return TIKU_ADC_OK on success, TIKU_ADC_ERR_PARAM for unsupported channels
 */
int tiku_adc_arch_channel_init(uint8_t channel) {
    /* Only external pins need GPIO config. The internal temp channel
     * is enabled lazily in read(). */
    if (channel <= 3U) {
        uint8_t pin = (uint8_t)(RP2350_ADC_GPIO_BASE + channel);
        /* High-impedance analog input: input buffer off, pulls off,
         * output disabled. Anything else loads the pin. */
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
 * needed, clears any sticky error, fires START_ONCE, and waits for READY
 * with a bounded spin. The 12-bit raw result is right-shifted by
 * adc_result_shift to match the resolution requested at init time.
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

    /* Build the new CS value: keep EN; flip TS_EN based on whether we
     * need the temperature sensor; clear sticky error; set AINSEL.
     * Writing 1 to ERR_STICKY clears it (W1C). */
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

    /* Wait for READY (idle). At full ADC clock this is ~2 us; bound
     * the spin so a wedged ADC doesn't lock the kernel. */
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
