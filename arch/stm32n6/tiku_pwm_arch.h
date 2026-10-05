/*
 * Tiku Operating System v0.06
 * Simple. Ubiquitous. Intelligence, Everywhere.
 * http://tiku-os.org
 *
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 *
 * tiku_pwm_arch.h - STM32N6 PWM on TIM1, four channels.
 *
 * The channels drive PE9, PE11, PE13 and PE14 on AF1, the only pins these
 * calls accept; the four share one counter and so one frequency.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#ifndef TIKU_STM32N6_PWM_ARCH_H_
#define TIKU_STM32N6_PWM_ARCH_H_

#include <stdint.h>

#define TIKU_PWM_OK             0  /**< success                             */
#define TIKU_PWM_ERR_INVALID   -1  /**< pin without a channel, or 0 Hz      */
#define TIKU_PWM_ERR_FREQ      -2  /**< no TIM1 clock, or rate out of range */

/**
 * @brief Start PWM on one of the TIM1 pins.
 *
 * @param gpio_pin  Port E pin: 9, 11, 13 or 14
 * @param freq_hz   PWM frequency, shared by all four channels
 * @param duty_u16  Duty scaled over 0..65535
 * @return TIKU_PWM_OK, TIKU_PWM_ERR_INVALID or TIKU_PWM_ERR_FREQ
 */
int tiku_pwm_arch_init(uint8_t gpio_pin, uint32_t freq_hz, uint16_t duty_u16);

/**
 * @brief Change the duty cycle of a running channel.
 *
 * @param gpio_pin  Port E pin: 9, 11, 13 or 14
 * @param duty_u16  Duty scaled over 0..65535
 * @return TIKU_PWM_OK or TIKU_PWM_ERR_INVALID
 */
int tiku_pwm_arch_set_duty(uint8_t gpio_pin, uint16_t duty_u16);

/**
 * @brief Stop a channel and leave its pin a push-pull output driven low.
 *
 * @param gpio_pin  Port E pin
 * @return TIKU_PWM_OK or TIKU_PWM_ERR_INVALID
 */
int tiku_pwm_arch_close(uint8_t gpio_pin);

/** @brief Duty last set. @param gpio_pin  Port E pin @return 0..65535 */
uint16_t tiku_pwm_arch_get_duty(uint8_t gpio_pin);

/** @brief Counter wrap value. @param gpio_pin  Port E pin @return ARR, or 0 */
uint16_t tiku_pwm_arch_get_top(uint8_t gpio_pin);

/**
 * @brief Whether a channel's output is enabled.
 *
 * @param gpio_pin  Port E pin
 * @return 1 when enabled, 0 when not or for a pin without a channel
 */
int tiku_pwm_arch_is_enabled(uint8_t gpio_pin);

#endif /* TIKU_STM32N6_PWM_ARCH_H_ */
