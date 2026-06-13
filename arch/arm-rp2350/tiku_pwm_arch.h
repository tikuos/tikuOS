/*
 * Tiku Operating System v0.05
 * Simple. Ubiquitous. Intelligence, Everywhere.
 * http://tiku-os.org
 *
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 *
 * tiku_pwm_arch.h - RP2350 PWM driver interface
 *
 * Drives the 12 PWM slices (datasheet §12.7). Each slice has two
 * channels, A and B, mapped to even- and odd-numbered GPIO pins.
 * The driver picks the slice + channel for the requested pin and
 * computes TOP + DIV from a target frequency such that the duty
 * resolution stays at 16 bits across all supported clk_sys
 * frequencies.
 *
 * Typical uses: LED dimming, servo/motor control, audio tone
 * generation, DAC-style pin output.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#ifndef TIKU_RP2350_PWM_ARCH_H_
#define TIKU_RP2350_PWM_ARCH_H_

#include <stdint.h>

/**
 * @brief Return codes for the PWM driver.
 *
 * TIKU_PWM_OK           — operation succeeded.
 * TIKU_PWM_ERR_INVALID  — gpio_pin is out of range (> 47).
 * TIKU_PWM_ERR_FREQ     — freq_hz is zero or outside the range
 *                         representable by the 8.4-bit DIV field at
 *                         the current clk_sys frequency.
 */
#define TIKU_PWM_OK             0
#define TIKU_PWM_ERR_INVALID   -1
#define TIKU_PWM_ERR_FREQ      -2   /* frequency out of representable range */

/**
 * @brief Configure a PWM channel on @p gpio_pin at @p freq_hz and
 *        @p duty_u16 (0..65535) and start it.
 *
 * Picks the slice + channel from the pin number (slice = (pin/2)%12,
 * channel = pin%1). Sets TOP = 65535 so duty resolution is the full
 * 16 bits; sets DIV so the wrap frequency matches freq_hz against
 * the live clk_sys rate.
 *
 * @param gpio_pin   GPIO 0..47
 * @param freq_hz    Wrap frequency in Hz (~10 Hz to ~clk_sys/65536)
 * @param duty_u16   Compare value, 0 = fully low, 65535 = fully high
 * @return TIKU_PWM_OK, TIKU_PWM_ERR_INVALID, TIKU_PWM_ERR_FREQ
 */
int tiku_pwm_arch_init(uint8_t  gpio_pin,
                       uint32_t freq_hz,
                       uint16_t duty_u16);

/**
 * @brief Update only the duty cycle of an already-initialised pin.
 *
 * Cheap: writes one CC half-word.  Does not stop or restart the
 * slice — the new duty applies on the next compare-match.
 *
 * @param gpio_pin  GPIO pin whose PWM channel to update (0..47).
 * @param duty_u16  New compare value (0 = fully low, 65535 = fully high).
 * @return TIKU_PWM_OK or TIKU_PWM_ERR_INVALID.
 */
int tiku_pwm_arch_set_duty(uint8_t gpio_pin, uint16_t duty_u16);

/**
 * @brief Disable the PWM channel for @p gpio_pin.
 *
 * Drives the pin low and stops the slice if both channels are now off.
 *
 * @param gpio_pin  GPIO pin to disable (0..47).
 * @return TIKU_PWM_OK or TIKU_PWM_ERR_INVALID.
 */
int tiku_pwm_arch_close(uint8_t gpio_pin);

/**
 * @brief Read back the current compare (duty) value for a pin.
 *
 * Used by tests and diagnostics to verify the CC register was
 * written correctly by tiku_pwm_arch_init() / set_duty().
 *
 * @param gpio_pin  GPIO pin to query (0..47).
 * @return Current CC compare value for that pin's channel.
 */
uint16_t tiku_pwm_arch_get_duty(uint8_t gpio_pin);

/**
 * @brief Read back the TOP (wrap) value for the slice owning a pin.
 *
 * Tests use this to verify that tiku_pwm_arch_init() computed the
 * correct wrap period for the requested frequency.
 *
 * @param gpio_pin  GPIO pin whose slice to query (0..47).
 * @return Current TOP register value for that slice.
 */
uint16_t tiku_pwm_arch_get_top(uint8_t gpio_pin);

/**
 * @brief Query whether the PWM slice owning a pin is running.
 *
 * @param gpio_pin  GPIO pin to check (0..47).
 * @return Non-zero if CSR.EN is set, 0 if the slice is stopped.
 */
int tiku_pwm_arch_is_enabled(uint8_t gpio_pin);

#endif /* TIKU_RP2350_PWM_ARCH_H_ */
