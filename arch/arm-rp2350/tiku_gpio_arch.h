/*
 * Tiku Operating System v0.04
 * Simple. Ubiquitous. Intelligence, Everywhere.
 * http://tiku-os.org
 *
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 *
 * tiku_gpio_arch.h - RP2350 GPIO port access
 *
 * The RP2350 has a single bank of 30+ pins. To stay shell/VFS-compatible
 * with the MSP430 layout (/dev/gpio/{1..4}/{0..7}) we expose four
 * virtual ports of 8 pins each:
 *   port 1 -> GP0..GP7
 *   port 2 -> GP8..GP15
 *   port 3 -> GP16..GP23
 *   port 4 -> GP24..GP31
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#ifndef TIKU_RP2350_GPIO_ARCH_H_
#define TIKU_RP2350_GPIO_ARCH_H_

#include <stdint.h>

int8_t tiku_gpio_arch_set_output(uint8_t port, uint8_t pin);
int8_t tiku_gpio_arch_set_input(uint8_t port, uint8_t pin);
int8_t tiku_gpio_arch_write(uint8_t port, uint8_t pin, uint8_t val);
int8_t tiku_gpio_arch_toggle(uint8_t port, uint8_t pin);
int8_t tiku_gpio_arch_read(uint8_t port, uint8_t pin);
int8_t tiku_gpio_arch_get_dir(uint8_t port, uint8_t pin);

/* Per-pin direct helpers (used by the LED macros). pin is 0..29. */
void tiku_rp2350_gpio_init_output(uint8_t pin);
void tiku_rp2350_gpio_set(uint8_t pin, uint8_t value);
void tiku_rp2350_gpio_toggle(uint8_t pin);

#endif /* TIKU_RP2350_GPIO_ARCH_H_ */
