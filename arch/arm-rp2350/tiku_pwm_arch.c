/*
 * Tiku Operating System v0.05
 * Simple. Ubiquitous. Intelligence, Everywhere.
 * http://tiku-os.org
 *
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 *
 * tiku_pwm_arch.c - RP2350 PWM driver
 *
 * The 12 slices share one PWM_EN register; each slice has its own
 * CSR / DIV / CTR / CC / TOP. We always set TOP = 0xFFFF so duty
 * resolution stays 16-bit, then choose DIV (8.4 fixed-point) so the
 * effective wrap rate matches the requested freq_hz against the live
 * clk_sys rate. That way a clk_sys retune (12/48/100/125/133/150 MHz
 * — see tiku_cpu_freq_boot_arch.c) doesn't change PWM behaviour
 * once init runs.
 *
 * Single-shot init per pin; calling init again on the same pin
 * reconfigures the channel.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include "tiku_pwm_arch.h"
#include "tiku_rp2350_regs.h"
#include <stddef.h>

extern unsigned long tiku_cpu_rp2350_clock_get_hz(void);

/** @brief Default TOP register value; sets 16-bit duty resolution. */
#define PWM_TOP_DEFAULT   0xFFFFU   /* 16-bit duty resolution */

/** @brief Tracks whether the PWM block has been taken out of reset. */
static uint8_t g_pwm_reset_done;

/*---------------------------------------------------------------------------*/
/* Helpers                                                                   */
/*---------------------------------------------------------------------------*/

/**
 * @brief Compute the 16.8 fixed-point divider for the requested wrap frequency.
 *
 * Derives DIV from:
 *   wrap_hz = clk_sys / (DIV * (TOP + 1))
 *   DIV     = clk_sys / (wrap_hz * (TOP + 1))
 * Returned divider is formatted for the SLICE_DIV register layout.
 *
 * @param freq_hz  Target PWM wrap frequency in Hz.
 * @return 12.4 fixed-point divider value (multiply of 16), or 0 if
 *         freq_hz is 0 or the requested frequency is out of range.
 */
static uint32_t pwm_compute_div(uint32_t freq_hz) {
    /* SLICE_DIV layout (datasheet §12.7.4.1): bits [11:4] integer
     * part, bits [3:0] fractional part — so 12.4 fixed-point in a
     * 16-bit field, written as a 32-bit access. The integer field
     * is 8 bits (range 1..255) on RP2040; RP2350 widens to 12 bits
     * (1..4095). Compute as 12.4 unconditionally; if the answer
     * exceeds 12 bits we clamp.
     *
     * Compute divider_x16 = clk_sys / (freq_hz * (TOP+1)) * 16. */
    uint64_t clk      = (uint64_t)tiku_cpu_rp2350_clock_get_hz();
    uint64_t denom    = (uint64_t)freq_hz * (uint64_t)(PWM_TOP_DEFAULT + 1U);
    if (denom == 0ULL) {
        return 0U;
    }
    uint64_t div_x16  = (clk * 16ULL + denom / 2ULL) / denom;
    if (div_x16 < 16ULL) {
        /* Less than divisor 1.0 -- saturate at min (the slice will
         * run at clk_sys / (TOP+1) which is the highest wrap rate). */
        div_x16 = 16ULL;
    }
    if (div_x16 > 0xFFFFULL) {
        /* Above 12.4 max -- caller's freq is too low for our chosen
         * TOP. Return 0 to signal "out of range" so caller can
         * pick a smaller TOP. */
        return 0U;
    }
    return (uint32_t)div_x16;
}

/**
 * @brief Take the PWM block out of reset exactly once per boot.
 *
 * Subsequent calls are no-ops; the guard is checked before issuing
 * the unreset to avoid redundant register writes.
 */
static void pwm_block_reset_once(void) {
    if (g_pwm_reset_done) {
        return;
    }
    rp2350_unreset(RP2350_RESETS_PWM);
    g_pwm_reset_done = 1U;
}

/**
 * @brief Route a GPIO pin to its associated PWM slice output.
 *
 * Configures the pad for 4 mA drive with input enable, then sets the
 * IO_BANK0 function select to PWM.
 *
 * @param gpio  GPIO pin number to mux to PWM.
 */
static void pwm_pin_route_to_slice(uint8_t gpio) {
    _RP2350_REG(RP2350_PADS_BANK0_GPIO(gpio)) =
        RP2350_PADS_DRIVE_4MA | RP2350_PADS_IE;
    _RP2350_REG(RP2350_IO_BANK0_GPIO_CTRL(gpio)) = RP2350_IO_FUNC_PWM;
}

/*---------------------------------------------------------------------------*/
/* HAL                                                                       */
/*---------------------------------------------------------------------------*/

/**
 * @brief Initialise a PWM output on the given GPIO pin.
 *
 * Takes the PWM block out of reset if needed, computes the clock
 * divider for freq_hz, programs TOP/DIV/CC, resets the counter, and
 * enables the slice.  Calling init again on the same pin reconfigures
 * the channel without disturbing the other channel in the slice.
 *
 * @param gpio_pin  GPIO pin to configure as a PWM output (0-based).
 * @param freq_hz   Desired PWM wrap frequency in Hz; must be non-zero.
 * @param duty_u16  Initial duty cycle as a 16-bit fraction of TOP
 *                  (0 = 0 %, 0xFFFF = ~100 %).
 * @return TIKU_PWM_OK on success, TIKU_PWM_ERR_INVALID if freq_hz is
 *         zero, or TIKU_PWM_ERR_FREQ if the frequency is out of range.
 */
int tiku_pwm_arch_init(uint8_t  gpio_pin,
                       uint32_t freq_hz,
                       uint16_t duty_u16) {
    uint8_t  slice;
    uint8_t  channel;
    uint32_t div;
    uint32_t cc;

    if (gpio_pin > 47U || freq_hz == 0U) {
        return TIKU_PWM_ERR_INVALID;
    }

    pwm_block_reset_once();

    slice   = rp2350_pwm_pin_to_slice(gpio_pin);
    channel = rp2350_pwm_pin_to_channel(gpio_pin);

    div = pwm_compute_div(freq_hz);
    if (div == 0U) {
        return TIKU_PWM_ERR_FREQ;
    }

    /* Disable the slice while we reconfigure, then re-enable. */
    _RP2350_REG(RP2350_PWM_SLICE_CSR(slice)) = 0U;

    _RP2350_REG(RP2350_PWM_SLICE_TOP(slice)) = PWM_TOP_DEFAULT;
    _RP2350_REG(RP2350_PWM_SLICE_DIV(slice)) = div;

    /* CC is a single 32-bit register holding both channels:
     *   bits [15:0]  channel A compare
     *   bits [31:16] channel B compare
     * Read-modify-write so the OTHER channel's value is preserved. */
    cc = _RP2350_REG(RP2350_PWM_SLICE_CC(slice));
    if (channel == 0U) {
        cc = (cc & 0xFFFF0000U) | (uint32_t)duty_u16;
    } else {
        cc = (cc & 0x0000FFFFU) | ((uint32_t)duty_u16 << 16);
    }
    _RP2350_REG(RP2350_PWM_SLICE_CC(slice)) = cc;

    /* Reset counter then enable. */
    _RP2350_REG(RP2350_PWM_SLICE_CTR(slice)) = 0U;
    _RP2350_REG(RP2350_PWM_SLICE_CSR(slice)) = RP2350_PWM_CSR_EN;

    /* Mux the GPIO to this slice's output. */
    pwm_pin_route_to_slice(gpio_pin);

    return TIKU_PWM_OK;
}

/**
 * @brief Update the duty cycle of a running PWM channel.
 *
 * Performs a read-modify-write on the shared CC register so the other
 * channel in the same slice is not disturbed.  The change takes effect
 * at the next counter wrap.
 *
 * @param gpio_pin  GPIO pin identifying the PWM channel to update.
 * @param duty_u16  New duty cycle as a 16-bit fraction of TOP
 *                  (0 = 0 %, 0xFFFF = ~100 %).
 * @return TIKU_PWM_OK always.
 */
int tiku_pwm_arch_set_duty(uint8_t gpio_pin, uint16_t duty_u16) {
    if (gpio_pin > 47U) {
        return TIKU_PWM_ERR_INVALID;
    }
    uint8_t  slice   = rp2350_pwm_pin_to_slice(gpio_pin);
    uint8_t  channel = rp2350_pwm_pin_to_channel(gpio_pin);
    uint32_t cc      = _RP2350_REG(RP2350_PWM_SLICE_CC(slice));

    if (channel == 0U) {
        cc = (cc & 0xFFFF0000U) | (uint32_t)duty_u16;
    } else {
        cc = (cc & 0x0000FFFFU) | ((uint32_t)duty_u16 << 16);
    }
    _RP2350_REG(RP2350_PWM_SLICE_CC(slice)) = cc;
    return TIKU_PWM_OK;
}

/**
 * @brief Stop PWM output on a pin and return it to SIO control.
 *
 * Sets the channel's compare value to 0 (level low).  Disables the
 * slice only when both channels are zero, so the sibling channel is
 * not disrupted.  Re-muxes the GPIO to SIO so the pin goes low.
 *
 * @param gpio_pin  GPIO pin identifying the PWM channel to close.
 * @return TIKU_PWM_OK always.
 */
int tiku_pwm_arch_close(uint8_t gpio_pin) {
    if (gpio_pin > 47U) {
        return TIKU_PWM_ERR_INVALID;
    }
    uint8_t  slice   = rp2350_pwm_pin_to_slice(gpio_pin);
    uint8_t  channel = rp2350_pwm_pin_to_channel(gpio_pin);
    uint32_t cc;

    /* Drop the channel's compare to 0 (level low). */
    cc = _RP2350_REG(RP2350_PWM_SLICE_CC(slice));
    if (channel == 0U) {
        cc &= 0xFFFF0000U;
    } else {
        cc &= 0x0000FFFFU;
    }
    _RP2350_REG(RP2350_PWM_SLICE_CC(slice)) = cc;

    /* Disable the slice only if BOTH channels are now 0 (callers
     * may still be using the other half). */
    if (cc == 0U) {
        _RP2350_REG(RP2350_PWM_SLICE_CSR(slice)) = 0U;
    }

    /* Return the pin to SIO so it goes low (or whatever the user
     * sets it to next). */
    _RP2350_REG(RP2350_IO_BANK0_GPIO_CTRL(gpio_pin)) = RP2350_IO_FUNC_SIO;

    return TIKU_PWM_OK;
}

/**
 * @brief Read the current compare value for a PWM channel.
 *
 * Extracts the correct 16-bit half of the shared CC register based on
 * whether the pin maps to channel A (bits 15:0) or B (bits 31:16).
 *
 * @param gpio_pin  GPIO pin identifying the PWM channel to query.
 * @return Current duty-cycle compare value (0 – 0xFFFF).
 */
uint16_t tiku_pwm_arch_get_duty(uint8_t gpio_pin) {
    if (gpio_pin > 47U) {
        return 0U;
    }
    uint8_t  slice   = rp2350_pwm_pin_to_slice(gpio_pin);
    uint8_t  channel = rp2350_pwm_pin_to_channel(gpio_pin);
    uint32_t cc      = _RP2350_REG(RP2350_PWM_SLICE_CC(slice));
    if (channel == 0U) {
        return (uint16_t)(cc & 0xFFFFU);
    }
    return (uint16_t)((cc >> 16) & 0xFFFFU);
}

/**
 * @brief Read the TOP (wrap) register for the slice owning a pin.
 *
 * @param gpio_pin  GPIO pin identifying the PWM slice to query.
 * @return Current TOP value (typically PWM_TOP_DEFAULT = 0xFFFF).
 */
uint16_t tiku_pwm_arch_get_top(uint8_t gpio_pin) {
    if (gpio_pin > 47U) {
        return 0U;
    }
    uint8_t slice = rp2350_pwm_pin_to_slice(gpio_pin);
    return (uint16_t)(_RP2350_REG(RP2350_PWM_SLICE_TOP(slice)) & 0xFFFFU);
}

/**
 * @brief Report whether the PWM slice for a pin is currently running.
 *
 * @param gpio_pin  GPIO pin identifying the PWM slice to check.
 * @return 1 if the slice CSR EN bit is set, 0 otherwise.
 */
int tiku_pwm_arch_is_enabled(uint8_t gpio_pin) {
    if (gpio_pin > 47U) {
        return 0;
    }
    uint8_t slice = rp2350_pwm_pin_to_slice(gpio_pin);
    return (_RP2350_REG(RP2350_PWM_SLICE_CSR(slice)) & RP2350_PWM_CSR_EN)
            ? 1 : 0;
}
