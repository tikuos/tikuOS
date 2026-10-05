/*
 * Tiku Operating System v0.06
 * Simple. Ubiquitous. Intelligence, Everywhere.
 * http://tiku-os.org
 *
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 *
 * tiku_pwm_arch.h - RP2350 PWM driver interface.
 *
 * Drives the 12 PWM slices, each with an A (even GPIO) and B (odd GPIO)
 * channel.  TOP is fixed at 0xFFFF for 16-bit duty resolution, and DIV is
 * computed from clk_sys for the requested frequency.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#ifndef TIKU_RP2350_PWM_ARCH_H_
#define TIKU_RP2350_PWM_ARCH_H_

#include <stdint.h>

#define TIKU_PWM_OK             0   /**< Operation succeeded */
#define TIKU_PWM_ERR_INVALID   -1   /**< gpio_pin > 47, or freq_hz is 0 */
#define TIKU_PWM_ERR_FREQ      -2   /**< freq_hz too low for the 8.4-bit DIV
                                         field at the current clk_sys */

/**
 * @brief Configure a PWM channel on @p gpio_pin at @p freq_hz and
 *        @p duty_u16 (0..65535) and start it.
 *
 * Takes the slice and channel from rp2350_pwm_pin_to_slice() and
 * rp2350_pwm_pin_to_channel().  TOP = 65535 gives 16-bit duty resolution, and
 * DIV is set from the live clk_sys so the wrap frequency matches @p freq_hz.
 *
 * @param gpio_pin   GPIO 0..47
 * @param freq_hz    Wrap frequency in Hz (~10 Hz to clk_sys/65536); a higher
 *                   frequency runs at clk_sys/65536
 * @param duty_u16   Compare value, 0 = always low, 65535 = ~100 % high
 * @return TIKU_PWM_OK, TIKU_PWM_ERR_INVALID, TIKU_PWM_ERR_FREQ
 */
int tiku_pwm_arch_init(uint8_t  gpio_pin,
                       uint32_t freq_hz,
                       uint16_t duty_u16);

/**
 * @brief Update only the duty cycle of an already-initialised pin.
 *
 * Rewrites the channel's half of CC without stopping the slice; the new value
 * takes effect at the next counter wrap.
 *
 * @param gpio_pin  GPIO pin whose PWM channel to update (0..47).
 * @param duty_u16  New compare value (0 = always low, 65535 = ~100 % high).
 * @return TIKU_PWM_OK or TIKU_PWM_ERR_INVALID.
 */
int tiku_pwm_arch_set_duty(uint8_t gpio_pin, uint16_t duty_u16);

/**
 * @brief Disable the PWM channel for @p gpio_pin.
 *
 * Sets the channel's compare value to 0, stops the slice if the other
 * channel's is 0 too, and returns the pin to SIO.
 *
 * @param gpio_pin  GPIO pin to disable (0..47).
 * @return TIKU_PWM_OK or TIKU_PWM_ERR_INVALID.
 */
int tiku_pwm_arch_close(uint8_t gpio_pin);

/**
 * @brief Read back the current compare (duty) value for a pin.
 *
 * @param gpio_pin  GPIO pin to query (0..47).
 * @return Current CC compare value for that pin's channel; 0 if
 *         gpio_pin > 47.
 */
uint16_t tiku_pwm_arch_get_duty(uint8_t gpio_pin);

/**
 * @brief Read back the TOP (wrap) value for the slice owning a pin.
 *
 * @param gpio_pin  GPIO pin whose slice to query (0..47).
 * @return Current TOP register value for that slice (0xFFFF after
 *         tiku_pwm_arch_init()); 0 if gpio_pin > 47.
 */
uint16_t tiku_pwm_arch_get_top(uint8_t gpio_pin);

/**
 * @brief Query whether the PWM slice owning a pin is running.
 *
 * @param gpio_pin  GPIO pin to check (0..47).
 * @return Non-zero if CSR.EN is set, 0 if the slice is stopped or
 *         gpio_pin > 47.
 */
int tiku_pwm_arch_is_enabled(uint8_t gpio_pin);

#endif /* TIKU_RP2350_PWM_ARCH_H_ */
