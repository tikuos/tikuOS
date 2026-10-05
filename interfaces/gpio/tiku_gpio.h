/*
 * Tiku Operating System v0.06
 * Simple. Ubiquitous. Intelligence, Everywhere.
 * http://tiku-os.org
 *
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 *
 * tiku_gpio.h - platform-agnostic raw GPIO interface.
 *
 * A port/pin-indexed API for kernel code that drives pins directly.
 * Header-only: every call is a static inline resolving to the arch driver.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#ifndef TIKU_GPIO_H_
#define TIKU_GPIO_H_

#include <stdint.h>
#if defined(PLATFORM_RP2350)
#include <arch/arm-rp2350/tiku_gpio_arch.h>
#elif defined(PLATFORM_AMBIQ)
#include <arch/ambiq/tiku_gpio_arch.h>
#elif defined(PLATFORM_NORDIC)
#include <arch/nordic/tiku_gpio_arch.h>
#elif defined(PLATFORM_STM32N6)
#include <arch/stm32n6/tiku_gpio_arch.h>
#elif defined(PLATFORM_RA8P1)
#include <arch/ra8p1/tiku_gpio_arch.h>
#elif defined(PLATFORM_ESP32C61)
#include <arch/esp32c61/tiku_gpio_arch.h>
#else
#include <arch/msp430/tiku_gpio_arch.h>
#endif
#include <hal/tiku_gpio_irq_hal.h>

/*---------------------------------------------------------------------------*/
/* RETURN CODES                                                              */
/*---------------------------------------------------------------------------*/

#define TIKU_GPIO_OK           0    /**< success                   */
#define TIKU_GPIO_ERR_INVALID -1    /**< bad port or pin           */

/*---------------------------------------------------------------------------*/
/* CORE API                                                                  */
/*---------------------------------------------------------------------------*/

/**
 * @brief Configure a pin as digital output.
 * @param port Platform port number (see tiku_gpio_geometry.h); 0xFF for port J
 * @param pin  Pin within port (see tiku_gpio_geometry.h for the platform width)
 * @return TIKU_GPIO_OK or TIKU_GPIO_ERR_INVALID
 */
static inline int tiku_gpio_dir_out(uint8_t port, uint8_t pin)
{
    return tiku_gpio_arch_set_output(port, pin);
}

/**
 * @brief Configure a pin as a digital input.
 *
 * The MSP430 and RP2350 ports also enable the pin's pull-up; the other ports
 * do not set one.
 */
static inline int tiku_gpio_dir_in(uint8_t port, uint8_t pin)
{
    return tiku_gpio_arch_set_input(port, pin);
}

/**
 * @brief Drive a pin high, making it an output if it is not one.
 */
static inline int tiku_gpio_set(uint8_t port, uint8_t pin)
{
    return tiku_gpio_arch_write(port, pin, 1);
}

/**
 * @brief Drive a pin low, making it an output if it is not one.
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
 * Equivalent to tiku_gpio_set/clear, selectable at runtime, and likewise
 * makes the pin an output.  tiku_bitbang drives its pins through this call.
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
