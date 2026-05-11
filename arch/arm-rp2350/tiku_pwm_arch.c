/*
 * Tiku Operating System v0.04
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

#define PWM_TOP_DEFAULT   0xFFFFU   /* 16-bit duty resolution */

static uint8_t g_pwm_reset_done;

/*---------------------------------------------------------------------------*/
/* Helpers                                                                   */
/*---------------------------------------------------------------------------*/

/* Compute the 16.8 fixed-point divider for the requested wrap frequency.
 *   wrap_hz = clk_sys / (DIV * (TOP + 1))
 *   DIV     = clk_sys / (wrap_hz * (TOP + 1))
 * Returned divider is shifted into the SLICE_DIV register layout
 * (integer in bits [19:8] -- wait, actually let me re-check). */
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

static void pwm_block_reset_once(void) {
    if (g_pwm_reset_done) {
        return;
    }
    rp2350_unreset(RP2350_RESETS_PWM);
    g_pwm_reset_done = 1U;
}

static void pwm_pin_route_to_slice(uint8_t gpio) {
    _RP2350_REG(RP2350_PADS_BANK0_GPIO(gpio)) =
        RP2350_PADS_DRIVE_4MA | RP2350_PADS_IE;
    _RP2350_REG(RP2350_IO_BANK0_GPIO_CTRL(gpio)) = RP2350_IO_FUNC_PWM;
}

/*---------------------------------------------------------------------------*/
/* HAL                                                                       */
/*---------------------------------------------------------------------------*/

int tiku_pwm_arch_init(uint8_t  gpio_pin,
                       uint32_t freq_hz,
                       uint16_t duty_u16) {
    uint8_t  slice;
    uint8_t  channel;
    uint32_t div;
    uint32_t cc;

    if (freq_hz == 0U) {
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

int tiku_pwm_arch_set_duty(uint8_t gpio_pin, uint16_t duty_u16) {
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

int tiku_pwm_arch_close(uint8_t gpio_pin) {
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

uint16_t tiku_pwm_arch_get_duty(uint8_t gpio_pin) {
    uint8_t  slice   = rp2350_pwm_pin_to_slice(gpio_pin);
    uint8_t  channel = rp2350_pwm_pin_to_channel(gpio_pin);
    uint32_t cc      = _RP2350_REG(RP2350_PWM_SLICE_CC(slice));
    if (channel == 0U) {
        return (uint16_t)(cc & 0xFFFFU);
    }
    return (uint16_t)((cc >> 16) & 0xFFFFU);
}

uint16_t tiku_pwm_arch_get_top(uint8_t gpio_pin) {
    uint8_t slice = rp2350_pwm_pin_to_slice(gpio_pin);
    return (uint16_t)(_RP2350_REG(RP2350_PWM_SLICE_TOP(slice)) & 0xFFFFU);
}

int tiku_pwm_arch_is_enabled(uint8_t gpio_pin) {
    uint8_t slice = rp2350_pwm_pin_to_slice(gpio_pin);
    return (_RP2350_REG(RP2350_PWM_SLICE_CSR(slice)) & RP2350_PWM_CSR_EN)
            ? 1 : 0;
}
