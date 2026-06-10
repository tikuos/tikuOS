/*
 * Tiku Operating System v0.05
 * Simple. Ubiquitous. Intelligence, Everywhere.
 * http://tiku-os.org
 *
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 *
 * tiku_gpio_arch.h - Apollo 510 GPIO access
 *
 * Two layers:
 *   1. The (port,pin)-indexed API the VFS/shell share with MSP430 and
 *      RP2350 (/dev/gpio/{1..N}/{0..7}). Apollo510 has >200 pads, far
 *      more than the 8-pin-per-port VFS view exposes; the low pads map
 *      port N pin p -> pad (N-1)*8 + p.
 *   2. Raw-pad helpers (tiku_ambiq_gpio_*) used by the board LED macros,
 *      because the EVB LEDs (165/89/92) sit well above the VFS range.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#ifndef TIKU_AMBIQ_GPIO_ARCH_H_
#define TIKU_AMBIQ_GPIO_ARCH_H_

#include <stdint.h>

/* Shared (port,pin) API — signatures match arch/arm-rp2350. */
int8_t tiku_gpio_arch_set_output(uint8_t port, uint8_t pin);
int8_t tiku_gpio_arch_set_input(uint8_t port, uint8_t pin);
int8_t tiku_gpio_arch_write(uint8_t port, uint8_t pin, uint8_t val);
int8_t tiku_gpio_arch_toggle(uint8_t port, uint8_t pin);
int8_t tiku_gpio_arch_read(uint8_t port, uint8_t pin);
int8_t tiku_gpio_arch_get_dir(uint8_t port, uint8_t pin);

/* Raw-pad helpers (pad number 0..AM_HAL_GPIO_MAX_PADS-1), used by the
 * board LED macros. */
void tiku_ambiq_gpio_init_output(uint32_t pad);
void tiku_ambiq_gpio_set(uint32_t pad, uint8_t value);
void tiku_ambiq_gpio_toggle(uint32_t pad);

#endif /* TIKU_AMBIQ_GPIO_ARCH_H_ */
