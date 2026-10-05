/*
 * Tiku Operating System v0.06
 * Simple. Ubiquitous. Intelligence, Everywhere.
 * http://tiku-os.org
 *
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 *
 * tiku_wake_arch.c - ESP32-C61 wake-source reporting.
 *
 * The wfi idle ends on any enabled CLIC line, so a source reads as armed
 * when its line is enabled.  Deep sleep's wake sources, and the light-sleep
 * idle's narrower set (timers and UART0), are not reported.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include <stddef.h>
#include <string.h>

#include <hal/tiku_wake_hal.h>

#include "tiku_irq_arch.h"
#include "tiku_esp32c61_regs.h"

/**
 * @brief Report which sources could wake the part.
 *
 * @param out  Receives the source set, and per port the pins armed for edges
 */
void tiku_wake_arch_query(tiku_wake_sources_t *out) {
    uint32_t on;

    if (out == NULL) {
        return;
    }
    memset(out, 0, sizeof(*out));
    on = tiku_esp32c61_irq_enabled();
    if (on & (1UL << TIKU_ESP32C61_LINE_TICK)) {
        out->sources |= TIKU_WAKE_SYSTICK;
    }
    if (on & (1UL << TIKU_ESP32C61_LINE_HTIMER)) {
        out->sources |= TIKU_WAKE_HTIMER;
    }
    if (on & (1UL << TIKU_ESP32C61_LINE_UART0)) {
        out->sources |= TIKU_WAKE_UART_RX;
    }
    if (on & (1UL << TIKU_ESP32C61_LINE_GPIO)) {
        out->sources |= TIKU_WAKE_GPIO;
    }
    /* Ports are banks of eight, as the GPIO contract numbers them. */
    for (unsigned n = 0U; n < ESP32C61_GPIO_COUNT; n++) {
        if (TIKU_REG32(ESP32C61_GPIO_PIN(n)) & ESP32C61_GPIO_PIN_ENA_PCPU) {
            out->gpio_ie[n / 8U] |= (uint8_t)(1U << (n % 8U));
        }
    }
}
