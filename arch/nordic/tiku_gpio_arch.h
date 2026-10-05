/*
 * Tiku Operating System v0.06
 * Simple. Ubiquitous. Intelligence, Everywhere.
 * http://tiku-os.org
 *
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 *
 * tiku_gpio_arch.h - nRF54L GPIO primitives (physical port/pin addressing).
 *
 * Helpers take a physical port (0..2, or 3 where P3 exists) and pin index
 * matching the board silk, so board headers can express LEDs and buttons
 * declaratively.  The VFS's virtual port numbering is a separate concern.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#ifndef TIKU_GPIO_ARCH_H_
#define TIKU_GPIO_ARCH_H_

#include <stdint.h>

/**
 * @brief Configure a pin as a push-pull output and drive an initial level.
 *
 * @param port       Physical port (0=P0, 1=P1, 2=P2, 3=P3 when present).
 * @param pin        Pin index (0..31).
 * @param init_level Initial output level (0 or 1).
 */
void tiku_nordic_gpio_init_output(uint8_t port, uint8_t pin, uint8_t init_level);

/**
 * @brief Configure a pin as an input with the internal pull-up enabled.
 *
 * @param port Physical port (0=P0, 1=P1, 2=P2, 3=P3 when present).
 * @param pin  Pin index (0..31).
 */
void tiku_nordic_gpio_init_input_pullup(uint8_t port, uint8_t pin);

/**
 * @brief Drive an output pin to a level.
 *
 * @param port  Physical port (0=P0, 1=P1, 2=P2, 3=P3 when present).
 * @param pin   Pin index (0..31).
 * @param level 0 = low, non-zero = high.
 */
void tiku_nordic_gpio_set(uint8_t port, uint8_t pin, uint8_t level);

/**
 * @brief Toggle an output pin.
 *
 * @param port Physical port (0=P0, 1=P1, 2=P2, 3=P3 when present).
 * @param pin  Pin index (0..31).
 */
void tiku_nordic_gpio_toggle(uint8_t port, uint8_t pin);

/**
 * @brief Read a pin's logical level: OUT for an output pin, IN for an input.
 *
 * @param port Physical port (0=P0, 1=P1, 2=P2, 3=P3 when present).
 * @param pin  Pin index (0..31).
 * @return 0 if low, 1 if high (0 for an unknown port).
 */
uint8_t tiku_nordic_gpio_read(uint8_t port, uint8_t pin);

/*---------------------------------------------------------------------------*/
/* Generic GPIO HAL (used by interfaces/gpio + the VFS /dev/gpio tree)       */
/*---------------------------------------------------------------------------*/

/*
 * These wrap the physical helpers above.  Ports are 1-based virtual ports
 * (1 = P0, 2 = P1, 3 = P2, 4 = P3 when present); pins are 0..31.
 * Return 0 on success, -1 if the port/pin is out of range; read/get_dir return
 * the pin level / direction (0 or 1), or -1 on range error.
 */

/**
 * @brief Claim a pin as a push-pull output, initially driven low.
 *
 * The nRF54LM20A/B fourth port P3 is exposed as virtual port 4.
 *
 * @param port Virtual port (1 = P0, 2 = P1, 3 = P2, 4 = P3 when present).
 * @param pin  Pin index (0..31).
 * @return 0 on success, -1 if the port/pin is out of range.
 */
int8_t tiku_gpio_arch_set_output(uint8_t port, uint8_t pin);

/**
 * @brief Claim a pin as a digital input.
 *
 * Writes an all-zero PIN_CNF: DIR = input, input buffer connected and no
 * pull resistor, so a floating pin reads indeterminately unless externally
 * biased.
 *
 * @param port Virtual port (1 = P0, 2 = P1, 3 = P2, 4 = P3 when present).
 * @param pin  Pin index (0..31).
 * @return 0 on success, -1 if the port/pin is out of range.
 */
int8_t tiku_gpio_arch_set_input(uint8_t port, uint8_t pin);

/**
 * @brief Drive a pin to @p val, claiming it as an output first.
 *
 * The level reaches OUT before DIR is set, so the pin never glitches to
 * the opposite state on the first write.
 *
 * @param port Virtual port (1 = P0, 2 = P1, 3 = P2, 4 = P3 when present).
 * @param pin  Pin index (0..31).
 * @param val  0 = low, non-zero = high.
 * @return 0 on success, -1 if the port/pin is out of range.
 */
int8_t tiku_gpio_arch_write(uint8_t port, uint8_t pin, uint8_t val);

/**
 * @brief Toggle a pin by read-modify-writing the port's OUT register.
 *
 * Does not change direction: the pin must already be an output for the
 * flipped level to reach the pad.
 *
 * @param port Virtual port (1 = P0, 2 = P1, 3 = P2, 4 = P3 when present).
 * @param pin  Pin index (0..31).
 * @return 0 on success, -1 if the port/pin is out of range.
 */
int8_t tiku_gpio_arch_toggle(uint8_t port, uint8_t pin);

/**
 * @brief Read a pin's logical level.
 *
 * Output pins are read back from OUT: an output's input buffer is left
 * disconnected, so IN reads 0 whatever is being driven.  Input pins read
 * from IN.
 *
 * @param port Virtual port (1 = P0, 2 = P1, 3 = P2, 4 = P3 when present).
 * @param pin  Pin index (0..31).
 * @return 0 or 1 on success, -1 if the port/pin is out of range.
 */
int8_t tiku_gpio_arch_read(uint8_t port, uint8_t pin);

/**
 * @brief Read a pin's configured direction from the DIR register.
 *
 * @param port Virtual port (1 = P0, 2 = P1, 3 = P2, 4 = P3 when present).
 * @param pin  Pin index (0..31).
 * @return 1 = output, 0 = input, -1 if the port/pin is out of range.
 */
int8_t tiku_gpio_arch_get_dir(uint8_t port, uint8_t pin);

/**
 * @brief Report whether PIN_CNF.CTRLSEL hands a pin to another controller.
 *
 * CTRLSEL other than GPIO gives the pin to VPR or GRTC.  TWIM, SPIM and UARTE
 * route pins through their own PSEL registers and leave CTRLSEL at GPIO, so
 * their pins read as free; tiku_gpio_owner.c reserves the console's itself.
 *
 * @param port Virtual port (1 = P0, 2 = P1, 3 = P2, 4 = P3 when present).
 * @param pin  Pin index (0..31).
 * @return 1 if handed to another controller, 0 otherwise, -1 if the port/pin
 *         is out of range.
 */
int tiku_gpio_arch_is_peripheral(uint8_t port, uint8_t pin);

#endif /* TIKU_GPIO_ARCH_H_ */
