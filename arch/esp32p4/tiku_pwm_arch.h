/*
 * Tiku Operating System v0.06
 * Simple. Persistent. Intelligent.
 * http://tiku-os.org
 *
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 *
 * tiku_pwm_arch.h - ESP32-P4 PWM on the LED PWM controller (LEDC).
 *
 * Eight channels, each on its own pad, share four timers on the 40 MHz
 * crystal, one per frequency; a timer counts 2^16 steps up to 610 Hz and
 * fewer above.
 *
 * SPDX-License-Identifier: Apache-2.0
 */
#ifndef TIKU_ESP32P4_PWM_ARCH_H_
#define TIKU_ESP32P4_PWM_ARCH_H_
#include <stdint.h>

#define TIKU_PWM_OK             0   /**< Operation succeeded */
#define TIKU_PWM_ERR_INVALID   -1   /**< Pin not free for PWM, or freq_hz 0 */
#define TIKU_PWM_ERR_FREQ      -2   /**< Above 156250 Hz (fewer than 8 bits) */
#define TIKU_PWM_ERR_BUSY      -3   /**< No channel, or no timer at that rate */

/**
 * @brief Drive @p gpio_pin at @p freq_hz with @p duty_u16 (0..65535) high.
 *
 * A pin already driven is reconfigured in place, from the timer's next
 * period.  The pin is claimed as "pwm"; the board LED pin is taken unless
 * the LP core holds it.
 *
 * @return TIKU_PWM_OK or a negative TIKU_PWM_ERR_*
 * @note Scheduler context only.
 */
int tiku_pwm_arch_init(uint8_t gpio_pin, uint32_t freq_hz, uint16_t duty_u16);
/** @brief Change the duty of a driven pin from the next period.
 *  @return TIKU_PWM_OK, or TIKU_PWM_ERR_INVALID if @p gpio_pin is not driven */
int tiku_pwm_arch_set_duty(uint8_t gpio_pin, uint16_t duty_u16);
/**
 * @brief Stop the channel on @p gpio_pin and give the pad back as it was.
 *
 * The timer pauses when no channel uses it; the controller is reset and its
 * clocks restored when the last channel closes.
 *
 * @return TIKU_PWM_OK, or TIKU_PWM_ERR_INVALID if @p gpio_pin is not driven
 */
int tiku_pwm_arch_close(uint8_t gpio_pin);
/** @brief The compare value scaled to 16 bits; 0 for a pin not driven. */
uint16_t tiku_pwm_arch_get_duty(uint8_t gpio_pin);
/** @brief The timer's last count, 2^bits - 1; 0 for a pin not driven. */
uint16_t tiku_pwm_arch_get_top(uint8_t gpio_pin);
/** @brief Non-zero while the pin's channel and its timer run. */
int tiku_pwm_arch_is_enabled(uint8_t gpio_pin);

#endif /* TIKU_ESP32P4_PWM_ARCH_H_ */
