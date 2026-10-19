/*
 * Tiku Operating System v0.06
 * Simple. Persistent. Intelligent.
 * http://tiku-os.org
 *
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 *
 * tiku_gpio_arch.c - P4 pad observation without changing unverified wiring.
 *
 * Ports 1..7, pins 0..7 map onto GPIO0..54 for level and output-enable
 * reads; output, input, write, toggle and pin interrupts are refused.
 * Register offsets: ESP-IDF 4d59230, ESP32-P4 hw_ver1.
 *
 * SPDX-License-Identifier: Apache-2.0
 */
#include "tiku_gpio_arch.h"
#include "tiku_esp32p4_regs.h"
#include <hal/tiku_gpio_irq_hal.h>

/** @brief Translate a virtual bank into a physical GPIO number. */
static int pad(uint8_t port, uint8_t pin)
{
    unsigned number;
    if (port < 1 || port > 7 || pin >= 8) return -1;
    number = (port - 1u) * 8u + pin;
    return number < 55 ? (int)number : -1;
}
int8_t tiku_gpio_arch_set_output(uint8_t port, uint8_t pin)
{
    (void)port; (void)pin;
    return -1;
}
int8_t tiku_gpio_arch_set_input(uint8_t port, uint8_t pin)
{
    (void)port; (void)pin;
    return -1;
}
int8_t tiku_gpio_arch_write(uint8_t port, uint8_t pin, uint8_t value)
{
    (void)port; (void)pin; (void)value;
    return -1;
}
int8_t tiku_gpio_arch_toggle(uint8_t port, uint8_t pin)
{
    (void)port; (void)pin;
    return -1;
}
int8_t tiku_gpio_arch_read(uint8_t port, uint8_t pin)
{
    int n = pad(port, pin);
    if (n < 0) return -1;
    return (TIKU_P4_REG_READ(n < 32 ? 0x500e003cu : 0x500e0040u) >> (n & 31)) & 1u;
}
int8_t tiku_gpio_arch_get_dir(uint8_t port, uint8_t pin)
{
    int n = pad(port, pin);
    if (n < 0) return -1;
    return (TIKU_P4_REG_READ(n < 32 ? 0x500e0020u : 0x500e002cu) >> (n & 31)) & 1u;
}
int tiku_gpio_arch_is_peripheral(uint8_t port, uint8_t pin)
{
    return pad(port, pin) < 0 ? -1 : 1;
}
int tiku_gpio_irq_arch_enable(uint8_t port, uint8_t pin, tiku_gpio_edge_t edge)
{
    if (pad(port, pin) < 0 || edge > TIKU_GPIO_EDGE_BOTH)
        return TIKU_GPIO_IRQ_ERR_INVALID;
    return TIKU_GPIO_IRQ_ERR_UNSUP;
}
int tiku_gpio_irq_arch_disable(uint8_t port, uint8_t pin)
{
    return pad(port, pin) < 0 ? TIKU_GPIO_IRQ_ERR_INVALID : TIKU_GPIO_IRQ_ERR_UNSUP;
}
