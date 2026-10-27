/*
 * Tiku Operating System v0.06
 * Simple. Persistent. Intelligent.
 * http://tiku-os.org
 *
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 *
 * tiku_gpio_arch.c - C6 protected GPIO routing and edge interrupts.
 *
 * SPDX-License-Identifier: Apache-2.0
 */
#include "tiku_gpio_arch.h"
#include "tiku_irq_arch.h"
#include <interfaces/gpio/tiku_gpio_owner.h>
#include <hal/tiku_gpio_irq_hal.h>
#include <kernel/process/tiku_process.h>
#define GPIO 0x60091000u
#define MUX 0x60090004u
#define GPIO_LINE 20u
#define GPIO_SOURCE 30u
static uint32_t armed;
static const char irq_owner[] = "gpio-irq";
/** @brief Map a one-based eight-pin port to GPIO0..30, or -1. */
static int number(uint8_t port, uint8_t pin)
{
    unsigned n = (port - 1u) * 8 + pin;
    return port >= 1 && port <= 4 && pin < 8 && n < 31 ? (int)n : -1;
}
/** @brief Protect SDIO, flash, wake, strapping, USB and boot console pins. */
static int reserved(unsigned n)
{
    return n == 2 || n == 4 || n == 5 || n == 8 || n == 9 || n == 12 || n == 13 || n >= 15;
}
int tiku_gpio_arch_is_peripheral(uint8_t port, uint8_t pin)
{
    int n = number(port, pin);
    if (n < 0)
        return -1;
    if (reserved(n))
        return 1;
    unsigned mux = (TIKU_C6_REG_READ(MUX + 4 * n) >> 12) & 7u;
    unsigned route = TIKU_C6_REG_READ(GPIO + 0x554 + 4 * n) & 255u;
    if (mux > 1 || (mux == 0 && n >= 4 && n <= 7) || route != 128u)
        return 1;
    for (unsigned i = 0; i < 128; i++) {
        unsigned in = TIKU_C6_REG_READ(GPIO + 0x154 + 4 * i);
        if ((in & 0xbfu) == (0x80u + (unsigned)n))
            return 1;
    }
    return 0;
}
/** @brief Select software GPIO and retain pull and drive settings. */
static void route_gpio(unsigned n)
{
    uint32_t value = TIKU_C6_REG_READ(MUX + 4 * n);
    TIKU_C6_REG_WRITE(MUX + 4 * n, (value & ~(7u << 12)) | (1u << 12) | (1u << 9));
    TIKU_C6_REG_WRITE(GPIO + 0x554 + 4 * n, 128u | (1u << 9));
    TIKU_C6_REG_WRITE(GPIO + 0x74 + 4 * n, TIKU_C6_REG_READ(GPIO + 0x74 + 4 * n) & ~(1u << 2));
}
int8_t tiku_gpio_arch_write(uint8_t port, uint8_t pin, uint8_t value)
{
    int n = number(port, pin);
    if (n < 0 || reserved(n))
        return -1;
    uint32_t state = TIKU_C6_IRQ_SAVE();
    if (tiku_gpio_arch_is_peripheral(port, pin)) {
        TIKU_C6_IRQ_RESTORE(state);
        return -1;
    }
    TIKU_C6_REG_WRITE(GPIO + (value ? 8u : 12u), 1u << n);
    route_gpio(n);
    TIKU_C6_REG_WRITE(GPIO + 0x24, 1u << n);
    TIKU_C6_IRQ_RESTORE(state);
    return 0;
}
int8_t tiku_gpio_arch_set_output(uint8_t port, uint8_t pin)
{
    return tiku_gpio_arch_write(port, pin, 0);
}
int8_t tiku_gpio_arch_set_input(uint8_t port, uint8_t pin)
{
    int n = number(port, pin);
    if (n < 0 || reserved(n))
        return -1;
    uint32_t state = TIKU_C6_IRQ_SAVE();
    if (tiku_gpio_arch_is_peripheral(port, pin)) {
        TIKU_C6_IRQ_RESTORE(state);
        return -1;
    }
    TIKU_C6_REG_WRITE(GPIO + 0x28, 1u << n);
    route_gpio(n);
    TIKU_C6_IRQ_RESTORE(state);
    return 0;
}
int8_t tiku_gpio_arch_read(uint8_t port, uint8_t pin)
{
    int n = number(port, pin);
    return n < 0 ? -1 : (int8_t)((TIKU_C6_REG_READ(GPIO + 0x3c) >> n) & 1u);
}
int8_t tiku_gpio_arch_get_dir(uint8_t port, uint8_t pin)
{
    int n = number(port, pin);
    return n < 0 ? -1 : (int8_t)((TIKU_C6_REG_READ(GPIO + 0x20) >> n) & 1u);
}
int8_t tiku_gpio_arch_toggle(uint8_t port, uint8_t pin)
{
    int n = number(port, pin);
    if (n < 0)
        return -1;
    uint32_t state = TIKU_C6_IRQ_SAVE();
    int8_t result = tiku_gpio_arch_write(port, pin, !(TIKU_C6_REG_READ(GPIO + 4) & (1u << n)));
    TIKU_C6_IRQ_RESTORE(state);
    return result;
}
/** @brief Clear armed edge flags before broadcasting their port/pin values. */
static void gpio_interrupt(void)
{
    uint32_t pending = TIKU_C6_REG_READ(GPIO + 0x44) & armed;
    TIKU_C6_REG_WRITE(GPIO + 0x4c, pending);
    for (unsigned n = 0; n < 31; n++)
        if (pending & (1u << n)) {
            (void)tiku_process_post(TIKU_PROCESS_BROADCAST, TIKU_EVENT_GPIO,
                                    (tiku_event_data_t)TIKU_GPIO_IRQ_PACK(n / 8u + 1u, n % 8u));
        }
}
int tiku_gpio_irq_arch_enable(uint8_t port, uint8_t pin, tiku_gpio_edge_t edge)
{
    int n = number(port, pin);
    if (n < 0 || (unsigned)edge > TIKU_GPIO_EDGE_BOTH)
        return TIKU_GPIO_IRQ_ERR_INVALID;
    if (reserved(n))
        return TIKU_GPIO_IRQ_ERR_UNSUP;
    uint32_t state = TIKU_C6_IRQ_SAVE();
    if (!armed && tiku_c6_irq_attach(GPIO_LINE, GPIO_SOURCE, 1, gpio_interrupt)) {
        TIKU_C6_IRQ_RESTORE(state);
        return TIKU_GPIO_IRQ_ERR_UNSUP;
    }
    if (tiku_gpio_claim(port, pin, irq_owner) || tiku_gpio_arch_set_input(port, pin)) {
        if (!armed)
            tiku_c6_irq_detach(GPIO_LINE);
        (void)tiku_gpio_release(port, pin, irq_owner);
        TIKU_C6_IRQ_RESTORE(state);
        return TIKU_GPIO_IRQ_ERR_UNSUP;
    }
    uint32_t address = GPIO + 0x74 + 4 * n, value = TIKU_C6_REG_READ(address);
    value = (value & ~((7u << 7) | (31u << 13))) | ((edge + 1u) << 7) | (1u << 13);
    TIKU_C6_REG_WRITE(GPIO + 0x4c, 1u << n);
    TIKU_C6_REG_WRITE(address, value);
    armed |= 1u << n;
    (void)tiku_c6_irq_enable(GPIO_LINE, 1);
    TIKU_C6_IRQ_RESTORE(state);
    return 0;
}
int tiku_gpio_irq_arch_disable(uint8_t port, uint8_t pin)
{
    int n = number(port, pin);
    if (n < 0)
        return TIKU_GPIO_IRQ_ERR_INVALID;
    if (reserved(n))
        return TIKU_GPIO_IRQ_ERR_UNSUP;
    uint32_t state = TIKU_C6_IRQ_SAVE();
    if (armed & (1u << n)) {
        uint32_t address = GPIO + 0x74 + 4 * n;
        TIKU_C6_REG_WRITE(address, TIKU_C6_REG_READ(address) & ~(31u << 13));
        TIKU_C6_REG_WRITE(GPIO + 0x4c, 1u << n);
        armed &= ~(1u << n);
        (void)tiku_gpio_release(port, pin, irq_owner);
        if (!armed)
            tiku_c6_irq_detach(GPIO_LINE);
    }
    TIKU_C6_IRQ_RESTORE(state);
    return 0;
}
uint32_t tiku_c6_gpio_irq_armed(void)
{
    return armed;
}
