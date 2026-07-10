/*
 * Tiku Operating System v0.05
 * Simple. Ubiquitous. Intelligence, Everywhere.
 * http://tiku-os.org
 *
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 *
 * tiku_gpio_arch.h - nRF54L GPIO primitives (physical port/pin addressing)
 *
 * Helpers take a PHYSICAL port number (0/1/2 == P0/P1/P2) and a pin index
 * (0..31) matching the board silk (P<port>.<pin>).  The nRF GPIO block uses
 * DIRSET/OUTSET/OUTCLR and a per-pin PIN_CNF[] register; these helpers wrap
 * that so board headers can express LEDs/buttons declaratively.  The virtual
 * VFS port numbering (1/2/3) is a separate concern handled in the VFS layer.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#ifndef TIKU_GPIO_ARCH_H_
#define TIKU_GPIO_ARCH_H_

#include <stdint.h>

/**
 * @brief Configure a pin as a push-pull output and drive an initial level.
 *
 * @param port       Physical port (0=P0, 1=P1, 2=P2).
 * @param pin        Pin index (0..31).
 * @param init_level Initial output level (0 or 1).
 */
void tiku_nordic_gpio_init_output(uint8_t port, uint8_t pin, uint8_t init_level);

/**
 * @brief Configure a pin as an input with the internal pull-up enabled.
 *
 * @param port Physical port (0=P0, 1=P1, 2=P2).
 * @param pin  Pin index (0..31).
 */
void tiku_nordic_gpio_init_input_pullup(uint8_t port, uint8_t pin);

/**
 * @brief Drive an output pin to a level.
 *
 * @param port  Physical port (0=P0, 1=P1, 2=P2).
 * @param pin   Pin index (0..31).
 * @param level 0 = low, non-zero = high.
 */
void tiku_nordic_gpio_set(uint8_t port, uint8_t pin, uint8_t level);

/**
 * @brief Toggle an output pin.
 *
 * @param port Physical port (0=P0, 1=P1, 2=P2).
 * @param pin  Pin index (0..31).
 */
void tiku_nordic_gpio_toggle(uint8_t port, uint8_t pin);

/**
 * @brief Read a pin's input level.
 *
 * @param port Physical port (0=P0, 1=P1, 2=P2).
 * @param pin  Pin index (0..31).
 * @return 0 if low, 1 if high.
 */
uint8_t tiku_nordic_gpio_read(uint8_t port, uint8_t pin);

/*---------------------------------------------------------------------------*/
/* Generic GPIO HAL (used by interfaces/gpio + the VFS /dev/gpio tree)       */
/*---------------------------------------------------------------------------*/

/*
 * These wrap the physical helpers above.  Ports are 1-based virtual ports
 * (1 = P0, 2 = P1, 3 = P2, matching TIKU_DEVICE_HAS_PORT1..3); pins are 0..31.
 * Return 0 on success, -1 if the port/pin is out of range; read/get_dir return
 * the pin level / direction (0 or 1), or -1 on range error.
 */
int8_t tiku_gpio_arch_set_output(uint8_t port, uint8_t pin);
int8_t tiku_gpio_arch_set_input(uint8_t port, uint8_t pin);
int8_t tiku_gpio_arch_write(uint8_t port, uint8_t pin, uint8_t val);
int8_t tiku_gpio_arch_toggle(uint8_t port, uint8_t pin);
int8_t tiku_gpio_arch_read(uint8_t port, uint8_t pin);
int8_t tiku_gpio_arch_get_dir(uint8_t port, uint8_t pin);

#endif /* TIKU_GPIO_ARCH_H_ */
