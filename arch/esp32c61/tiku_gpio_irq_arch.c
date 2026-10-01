/*
 * Tiku Operating System v0.06
 * Simple. Ubiquitous. Intelligence, Everywhere.
 * http://tiku-os.org
 *
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 *
 * tiku_gpio_irq_arch.c - ESP32-C61 pin edge interrupts.
 *
 * Every pin can interrupt: each picks its edge in its own PIN register and
 * all of them share one matrix source, so one line serves the whole bank.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include <stddef.h>

#include <hal/tiku_gpio_irq_hal.h>
#include <interfaces/gpio/tiku_gpio.h>
#include <kernel/process/tiku_process.h>

#include "tiku_gpio_arch.h"
#include "tiku_irq_arch.h"
#include "tiku_esp32c61_regs.h"

static volatile uint32_t gpio_armed;
static uint8_t gpio_line_ready;

/** @brief Every armed pin that fired: clear, then post one event each. */
static void gpio_isr(void) {
    uint32_t st = TIKU_REG32(ESP32C61_GPIO_PCPU_INT) & gpio_armed;

    /* Cleared before the posts: an edge arriving meanwhile sets the bit
     * again and is serviced again rather than swallowed. */
    TIKU_REG32(ESP32C61_GPIO_STATUS_W1TC) = st;
    while (st != 0UL) {
        unsigned n = (unsigned)__builtin_ctz(st);

        st &= st - 1UL;
        tiku_process_post(TIKU_PROCESS_BROADCAST, TIKU_EVENT_GPIO,
                          (tiku_event_data_t)
                          TIKU_GPIO_IRQ_PACK(n / 8U + 1U, n % 8U));
    }
}

int tiku_gpio_irq_arch_enable(uint8_t port, uint8_t pin,
                              tiku_gpio_edge_t edge) {
    int n = tiku_esp32c61_gpio_num(port, pin);
    uint32_t type, reg, s;

    if (n < 0) {
        return TIKU_GPIO_IRQ_ERR_INVALID;
    }
    switch (edge) {
    case TIKU_GPIO_EDGE_RISING:  type = 1UL; break;
    case TIKU_GPIO_EDGE_FALLING: type = 2UL; break;
    case TIKU_GPIO_EDGE_BOTH:    type = 3UL; break;
    default:                     return TIKU_GPIO_IRQ_ERR_INVALID;
    }
    /* The edge detector watches the pad, so the pin has to be an input. */
    (void)tiku_gpio_arch_set_input(port, pin);
    reg = TIKU_REG32(ESP32C61_GPIO_PIN(n)) &
          ~(ESP32C61_GPIO_PIN_TYPE_MSK | ESP32C61_GPIO_PIN_ENA_MSK);
    reg |= type << ESP32C61_GPIO_PIN_TYPE_POS;
    TIKU_REG32(ESP32C61_GPIO_PIN(n)) = reg;
    /* Drop whatever the pad did while it was configured, so arming does not
     * deliver an edge nobody caused. */
    TIKU_REG32(ESP32C61_GPIO_STATUS_W1TC) = 1UL << n;

    s = tiku_esp32c61_mie_off();
    gpio_armed |= 1UL << n;
    TIKU_REG32(ESP32C61_GPIO_PIN(n)) = reg | ESP32C61_GPIO_PIN_ENA_PCPU;
    if (!gpio_line_ready) {
        tiku_esp32c61_irq_attach(TIKU_ESP32C61_LINE_GPIO, ESP32C61_SRC_GPIO,
                                 TIKU_ESP32C61_LEVEL_DEFAULT, gpio_isr);
        tiku_esp32c61_irq_enable(TIKU_ESP32C61_LINE_GPIO);
        gpio_line_ready = 1U;
    }
    tiku_esp32c61_mie_restore(s);
    return TIKU_GPIO_IRQ_OK;
}

int tiku_gpio_irq_arch_disable(uint8_t port, uint8_t pin) {
    int n = tiku_esp32c61_gpio_num(port, pin);
    uint32_t s;

    if (n < 0) {
        return TIKU_GPIO_IRQ_ERR_INVALID;
    }
    s = tiku_esp32c61_mie_off();
    TIKU_REG32(ESP32C61_GPIO_PIN(n)) &=
        ~(ESP32C61_GPIO_PIN_TYPE_MSK | ESP32C61_GPIO_PIN_ENA_MSK);
    TIKU_REG32(ESP32C61_GPIO_STATUS_W1TC) = 1UL << n;
    gpio_armed &= ~(1UL << n);
    tiku_esp32c61_mie_restore(s);
    return TIKU_GPIO_IRQ_OK;
}
