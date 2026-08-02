/*
 * Tiku Operating System v0.06
 * Simple. Ubiquitous. Intelligence, Everywhere.
 * http://tiku-os.org
 *
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 *
 * tiku_wake_arch.c - STM32N6 wake-source reporting.
 *
 * Nothing is armed to wake the part yet, so the report is empty rather than
 * a list the hardware would not honour.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include <stddef.h>

#include <hal/tiku_wake_hal.h>

/**
 * @brief Report which sources could wake the part.
 *
 * @param out  Receives the source set; left empty on this port
 */
void tiku_wake_arch_query(tiku_wake_sources_t *out) {
    if (out == NULL) {
        return;
    }
    out->sources = 0U;
    for (unsigned i = 0; i < TIKU_WAKE_MAX_GPIO_PORTS; i++) {
        out->gpio_ie[i] = 0U;
    }
}
