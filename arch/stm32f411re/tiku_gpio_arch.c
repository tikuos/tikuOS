/*
 * Tiku Operating System v0.04
 * Simple. Ubiquitous. Intelligence, Everywhere.
 * http://tiku-os.org
 *
 * Authors: Jeremy Goh
 *
 * tiku_gpio_arch.c - STM32F411RE GPIO backend
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include "tiku_gpio_arch.h"
#include "tiku_pinmux_arch.h"
#include "tiku_stm32f411_regs.h"
#include <stdint.h>

/*---------------------------------------------------------------------------*/
/* Per-pin direct helpers                                                    */
/*---------------------------------------------------------------------------*/

/*
 * Board-level helpers and non-GPIO peripherals use the STM32's native
 * bank/pin numbering (PA..PD, pin 0..15). The platform-agnostic GPIO HAL
 * keeps the shell/VFS contract of 8 pins per port, so the HAL entry points
 * below translate virtual port+pin pairs before touching the hardware.
 */
static int
stm32f411_gpio_virtual_resolve(uint8_t port, uint8_t pin,
                               uint8_t *phys_port, uint8_t *phys_pin)
{
    if (pin > 7U || phys_port == (uint8_t *)0 || phys_pin == (uint8_t *)0) {
        return -1;
    }

    switch (port) {
    case 1U:
    case 3U:
    case 5U:
    case 7U:
        *phys_port = (uint8_t)(((port - 1U) / 2U) + 1U);
        *phys_pin  = pin;
        return 0;
    case 2U:
    case 4U:
    case 6U:
    case 8U:
        *phys_port = (uint8_t)(((port - 2U) / 2U) + 1U);
        *phys_pin  = (uint8_t)(pin + 8U);
        return 0;
    default:
        return -1;
    }
}

static uint32_t stm32f411_gpio_is_output(uint32_t gpio_base, uint8_t pin)
{
    uint32_t moder = _STM32F411_REG(STM32F411_GPIO_MODER(gpio_base));
    return (((moder >> (pin * 2U)) & 0x3U) == STM32F411_GPIO_MODE_OUTPUT);
}

static void stm32f411_gpio_set_raw(uint32_t gpio_base, uint8_t pin,
                                   uint8_t value)
{
    _STM32F411_REG(STM32F411_GPIO_BSRR(gpio_base)) =
        value ? STM32F411_GPIO_BSRR_SET(pin) : STM32F411_GPIO_BSRR_CLR(pin);
}

static void stm32f411_gpio_toggle_raw(uint32_t gpio_base, uint8_t pin)
{
    uint32_t odr = _STM32F411_REG(STM32F411_GPIO_ODR(gpio_base));
    _STM32F411_REG(STM32F411_GPIO_BSRR(gpio_base)) =
        (odr & STM32F411_BIT(pin))
            ? STM32F411_GPIO_BSRR_CLR(pin)
            : STM32F411_GPIO_BSRR_SET(pin);
}

void tiku_stm32f411_gpio_set(uint8_t port, uint8_t pin, uint8_t value)
{
    uint32_t gpio_base;
    uint32_t rcc_bit;

    if (tiku_stm32f411_pinmux_resolve(port, pin, &gpio_base, &rcc_bit) != 0) {
        return;
    }
    stm32f411_rcc_enable_ahb1(rcc_bit);
    if (!stm32f411_gpio_is_output(gpio_base, pin)) {
        if (tiku_stm32f411_pinmux_init_output(port, pin) != 0) {
            return;
        }
    }
    stm32f411_gpio_set_raw(gpio_base, pin, value);
}

void tiku_stm32f411_gpio_toggle(uint8_t port, uint8_t pin)
{
    uint32_t gpio_base;
    uint32_t rcc_bit;

    if (tiku_stm32f411_pinmux_resolve(port, pin, &gpio_base, &rcc_bit) != 0) {
        return;
    }
    stm32f411_rcc_enable_ahb1(rcc_bit);
    if (!stm32f411_gpio_is_output(gpio_base, pin)) {
        if (tiku_stm32f411_pinmux_init_output(port, pin) != 0) {
            return;
        }
    }
    stm32f411_gpio_toggle_raw(gpio_base, pin);
}

/*---------------------------------------------------------------------------*/
/* HAL entry points                                                          */
/*---------------------------------------------------------------------------*/

int8_t tiku_gpio_arch_set_output(uint8_t port, uint8_t pin)
{
    uint8_t phys_port;
    uint8_t phys_pin;

    if (stm32f411_gpio_virtual_resolve(port, pin, &phys_port, &phys_pin) != 0) {
        return -1;
    }
    return tiku_stm32f411_pinmux_init_output(phys_port, phys_pin);
}

int8_t tiku_gpio_arch_set_input(uint8_t port, uint8_t pin)
{
    uint8_t phys_port;
    uint8_t phys_pin;

    if (stm32f411_gpio_virtual_resolve(port, pin, &phys_port, &phys_pin) != 0) {
        return -1;
    }
    return tiku_stm32f411_pinmux_init_input(phys_port, phys_pin,
                                            STM32F411_GPIO_PUPD_UP);
}

int8_t tiku_gpio_arch_write(uint8_t port, uint8_t pin, uint8_t val)
{
    uint32_t gpio_base;
    uint32_t rcc_bit;
    uint8_t phys_port;
    uint8_t phys_pin;

    // Get actual port mappings (from virtual ports) and resolve that to GPIO base and RCC bit
    // The mappings to the actual ports are needed to support other peripherals (which use these 
    // mappings for configuration)
    if (stm32f411_gpio_virtual_resolve(port, pin, &phys_port, &phys_pin) != 0) {
        return -1;
    }
    if (tiku_stm32f411_pinmux_resolve(phys_port, phys_pin,
                                      &gpio_base, &rcc_bit) != 0) {
        return -1;
    }
    stm32f411_rcc_enable_ahb1(rcc_bit);
    if (!stm32f411_gpio_is_output(gpio_base, phys_pin)) {
        if (tiku_stm32f411_pinmux_init_output(phys_port, phys_pin) != 0) {
            return -1;
        }
    }
    stm32f411_gpio_set_raw(gpio_base, phys_pin, val);
    return 0;
}

int8_t tiku_gpio_arch_toggle(uint8_t port, uint8_t pin)
{
    uint32_t gpio_base;
    uint32_t rcc_bit;
    uint8_t phys_port;
    uint8_t phys_pin;

    if (stm32f411_gpio_virtual_resolve(port, pin, &phys_port, &phys_pin) != 0) {
        return -1;
    }
    if (tiku_stm32f411_pinmux_resolve(phys_port, phys_pin,
                                      &gpio_base, &rcc_bit) != 0) {
        return -1;
    }
    stm32f411_rcc_enable_ahb1(rcc_bit);
    if (!stm32f411_gpio_is_output(gpio_base, phys_pin)) {
        if (tiku_stm32f411_pinmux_init_output(phys_port, phys_pin) != 0) {
            return -1;
        }
    }
    stm32f411_gpio_toggle_raw(gpio_base, phys_pin);
    return 0;
}

int8_t tiku_gpio_arch_read(uint8_t port, uint8_t pin)
{
    uint32_t gpio_base;
    uint32_t rcc_bit;
    uint8_t phys_port;
    uint8_t phys_pin;

    if (stm32f411_gpio_virtual_resolve(port, pin, &phys_port, &phys_pin) != 0) {
        return -1;
    }
    if (tiku_stm32f411_pinmux_resolve(phys_port, phys_pin,
                                      &gpio_base, &rcc_bit) != 0) {
        return -1;
    }
    stm32f411_rcc_enable_ahb1(rcc_bit);
    return (_STM32F411_REG(STM32F411_GPIO_IDR(gpio_base))
            & STM32F411_BIT(phys_pin))
        ? 1 : 0;
}

int8_t tiku_gpio_arch_get_dir(uint8_t port, uint8_t pin)
{
    uint32_t gpio_base;
    uint32_t rcc_bit;
    uint32_t moder;
    uint8_t phys_port;
    uint8_t phys_pin;

    if (stm32f411_gpio_virtual_resolve(port, pin, &phys_port, &phys_pin) != 0) {
        return -1;
    }
    if (tiku_stm32f411_pinmux_resolve(phys_port, phys_pin,
                                      &gpio_base, &rcc_bit) != 0) {
        return -1;
    }
    stm32f411_rcc_enable_ahb1(rcc_bit);
    moder = _STM32F411_REG(STM32F411_GPIO_MODER(gpio_base));
    return (((moder >> (phys_pin * 2U)) & 0x3U) == STM32F411_GPIO_MODE_OUTPUT)
        ? 1 : 0;
}
