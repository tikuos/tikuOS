/*
 * Tiku Operating System v0.06
 * Simple. Ubiquitous. Intelligence, Everywhere.
 * http://tiku-os.org
 *
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 *
 * tiku_wake_arch.c - STM32N6 wake-source reporting.
 *
 * This port arms no wake source: tiku_wake_arch_query() reports an empty
 * set.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include <stddef.h>

#include <hal/tiku_wake_hal.h>

/**
 * @brief Report which sources could wake the part.
 *
 * @param out  Receives an empty set: sources 0 and every gpio_ie mask 0;
 *             NULL is ignored
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
