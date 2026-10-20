/*
 * Tiku Operating System v0.06
 * Simple. Persistent. Intelligent.
 * http://tiku-os.org
 *
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 *
 * tiku_gpio_arch.h - P4 GPIO matrix and IO MUX, pin edge interrupts, board LED.
 *
 * GPIO0..54 appear as seven virtual eight-pin ports numbered 1..7.  The
 * board header's reserved pads and pads routed to a peripheral refuse every
 * change; their levels and directions stay readable.
 *
 * SPDX-License-Identifier: Apache-2.0
 */
#ifndef TIKU_P4_GPIO_ARCH_H_
#define TIKU_P4_GPIO_ARCH_H_
#include <stdint.h>
/** @brief Make a free pad a low output.  @return 0, or -1 when refused. */
int8_t tiku_gpio_arch_set_output(uint8_t port, uint8_t pin);
/** @brief Make a free pad an input with its pulls unchanged; 0 or -1. */
int8_t tiku_gpio_arch_set_input(uint8_t port, uint8_t pin);
/** @brief Drive a free pad to @p value as an output; 0 or -1. */
int8_t tiku_gpio_arch_write(uint8_t port, uint8_t pin, uint8_t value);
/** @brief Invert a free pad's output latch and drive it; 0 or -1. */
int8_t tiku_gpio_arch_toggle(uint8_t port, uint8_t pin);
/** @brief Read a pad's input level: 0 or 1, or -1 for an invalid pin.  A pad
 *         with its input buffer off reads 0. */
int8_t tiku_gpio_arch_read(uint8_t port, uint8_t pin);
/** @brief Read the GPIO output-enable latch, without changing it. */
int8_t tiku_gpio_arch_get_dir(uint8_t port, uint8_t pin);
/** @brief Return 1 for a reserved or routed pad, 0 for a free GPIO, -1 for an
 *         invalid pin. */
int tiku_gpio_arch_is_peripheral(uint8_t port, uint8_t pin);
/** @brief The GPIO numbers with an edge interrupt armed, one bit each. */
uint64_t tiku_p4_gpio_irq_armed(void);
/** @brief Make the board LED pad a low output, unless the LP core holds it.
 *         Defined on a board with TIKU_BOARD_LED_PIN. */
void tiku_p4_led_init(void);
/** @brief Switch the board LED: 1 on, 0 off, -1 toggle.  No effect while the
 *         LP core holds the pad. */
void tiku_p4_led_set(int on);
/** @brief Make the button pad an input with its pull-up on.  Defined on a
 *         board with TIKU_BOARD_BTN_PIN. */
void tiku_p4_button_init(void);
/** @brief 1 while the button holds its pad low, else 0. */
uint8_t tiku_p4_button_pressed(void);
#endif
