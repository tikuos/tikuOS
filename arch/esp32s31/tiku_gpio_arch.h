/*
 * Tiku Operating System v0.06
 * Simple. Persistent. Intelligent.
 * http://tiku-os.org
 *
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 *
 * tiku_gpio_arch.h - S31 read-only GPIO inspection for the namespace.
 *
 * SPDX-License-Identifier: Apache-2.0
 */
#ifndef TIKU_S31_GPIO_ARCH_H_
#define TIKU_S31_GPIO_ARCH_H_
#include <stdint.h>
/** @brief Read a GPIO level or direction; return -1 outside GPIO0..61. */
static inline int tiku_s31_gpio_inspect(uint8_t port, uint8_t pin, unsigned direction)
{
    if (!port || port > 8 || pin > 7 || (port == 8 && pin > 5)) return -1;
    unsigned gpio = (port - 1u) * 8u + pin;
    uintptr_t reg = 0x20583000u + (direction ? 0x34u : 0x64u);
    if (gpio >= 32) reg += direction ? 12u : 4u;
    return (*(volatile uint32_t *)reg >> (gpio & 31u)) & 1u;
}
static inline int tiku_gpio_arch_read(uint8_t p, uint8_t n)
{ return tiku_s31_gpio_inspect(p, n, 0); }
static inline int tiku_gpio_arch_get_dir(uint8_t p, uint8_t n)
{ return tiku_s31_gpio_inspect(p, n, 1); }
/** @brief Reject raw reconfiguration; S31 peripherals own their pad configuration. */
static inline int tiku_gpio_arch_set_output(uint8_t p, uint8_t n)
{ (void)p; (void)n; return -1; }
static inline int tiku_gpio_arch_set_input(uint8_t p, uint8_t n)
{ (void)p; (void)n; return -1; }
static inline int tiku_gpio_arch_write(uint8_t p, uint8_t n, uint8_t v)
{ (void)p; (void)n; (void)v; return -1; }
static inline int tiku_gpio_arch_toggle(uint8_t p, uint8_t n)
{ (void)p; (void)n; return -1; }
#endif
