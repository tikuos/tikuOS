/*
 * Tiku Operating System v0.02
 * Simple. Ubiquitous. Intelligence, Everywhere.
 * http://tiku-os.org
 *
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 *
 * tiku_gpio.h - Platform-agnostic raw GPIO interface
 *
 * Thin pass-through over the architecture GPIO driver. Provides a
 * stable, port/pin-indexed API for kernel code that needs direct
 * pin control (e.g. bit-bang transmitters, software protocols)
 * without taking a dependency on the per-board LED indirection.
 *
 * Header-only: every call is a static inline that resolves to the
 * arch implementation at compile time. Cost is identical to calling
 * the arch driver directly; the indirection exists only so that
 * higher-level modules read as platform-agnostic.
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at:
 *
 *     http://www.apache.org/licenses/LICENSE-2.0
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#ifndef TIKU_GPIO_H_
#define TIKU_GPIO_H_

#include <stdint.h>
#include <arch/msp430/tiku_gpio_arch.h>
#include <hal/tiku_gpio_irq_hal.h>

/*---------------------------------------------------------------------------*/
/* RETURN CODES                                                              */
/*---------------------------------------------------------------------------*/

#define TIKU_GPIO_OK           0
#define TIKU_GPIO_ERR_INVALID -1

/*---------------------------------------------------------------------------*/
/* CORE API                                                                  */
/*---------------------------------------------------------------------------*/

/**
 * @brief Configure a pin as digital output.
 * @param port Port number (1..N for numbered ports; 0xFF for port J)
 * @param pin  Pin within port (0..7)
 * @return TIKU_GPIO_OK or TIKU_GPIO_ERR_INVALID
 */
static inline int tiku_gpio_dir_out(uint8_t port, uint8_t pin)
{
    return tiku_gpio_arch_set_output(port, pin);
}

/**
 * @brief Configure a pin as digital input with pull-up.
 */
static inline int tiku_gpio_dir_in(uint8_t port, uint8_t pin)
{
    return tiku_gpio_arch_set_input(port, pin);
}

/**
 * @brief Drive a pin high.
 *
 * Side effect: sets pin direction to output if not already.
 */
static inline int tiku_gpio_set(uint8_t port, uint8_t pin)
{
    return tiku_gpio_arch_write(port, pin, 1);
}

/**
 * @brief Drive a pin low.
 */
static inline int tiku_gpio_clear(uint8_t port, uint8_t pin)
{
    return tiku_gpio_arch_write(port, pin, 0);
}

/**
 * @brief Toggle a pin's level.
 */
static inline int tiku_gpio_toggle(uint8_t port, uint8_t pin)
{
    return tiku_gpio_arch_toggle(port, pin);
}

/**
 * @brief Drive a pin to the given value (0 or 1).
 *
 * Equivalent to tiku_gpio_set/clear but selectable at runtime.
 * This is the hot-path call used by tiku_bitbang.
 */
static inline int tiku_gpio_write(uint8_t port, uint8_t pin, uint8_t val)
{
    return tiku_gpio_arch_write(port, pin, val);
}

/**
 * @brief Read a pin's input level.
 * @return 0 or 1 on success, TIKU_GPIO_ERR_INVALID on bad port/pin
 */
static inline int tiku_gpio_read(uint8_t port, uint8_t pin)
{
    return tiku_gpio_arch_read(port, pin);
}

/**
 * @brief Read a pin's configured direction.
 * @return 1 = output, 0 = input, TIKU_GPIO_ERR_INVALID on bad port/pin
 */
static inline int tiku_gpio_get_dir(uint8_t port, uint8_t pin)
{
    return tiku_gpio_arch_get_dir(port, pin);
}

/**
 * @brief Enable an edge interrupt on a pin.
 *
 * Subsequent matching edges post a TIKU_EVENT_GPIO broadcast
 * event with port/pin packed in the data word; use
 * TIKU_GPIO_IRQ_PORT() / TIKU_GPIO_IRQ_PIN() to decode.
 */
static inline int tiku_gpio_irq_enable(uint8_t port, uint8_t pin,
                                       tiku_gpio_edge_t edge)
{
    return tiku_gpio_irq_arch_enable(port, pin, edge);
}

/** @brief Disable a previously enabled GPIO interrupt. */
static inline int tiku_gpio_irq_disable(uint8_t port, uint8_t pin)
{
    return tiku_gpio_irq_arch_disable(port, pin);
}

#endif /* TIKU_GPIO_H_ */
