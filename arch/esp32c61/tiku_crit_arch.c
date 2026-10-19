/*
 * Tiku Operating System v0.06
 * Simple. Persistent. Intelligent.
 * http://tiku-os.org
 *
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 *
 * tiku_crit_arch.c - ESP32-C61 critical sections over the CLIC lines.
 *
 * Snapshots the enabled lines, disables all but the ones the caller asks to
 * preserve, and restores the snapshot on exit.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include <hal/tiku_crit_hal.h>
#include <kernel/timers/tiku_crit.h>
#include <stdint.h>

#include "tiku_irq_arch.h"

/** @brief The lines enabled when the window opened. */
static uint32_t crit_saved;

void tiku_crit_arch_mask_irqs(uint8_t preserve_mask) {
    uint32_t keep = 0UL;

    /* Each flag keeps one CLIC line: the tick, the htimer, pin edges or
     * UART0; every other enabled line is disabled. */
    if (preserve_mask & TIKU_CRIT_PRESERVE_TICK) {
        keep |= 1UL << TIKU_ESP32C61_LINE_TICK;
    }
    if (preserve_mask & TIKU_CRIT_PRESERVE_HTIMER) {
        keep |= 1UL << TIKU_ESP32C61_LINE_HTIMER;
    }
    if (preserve_mask & TIKU_CRIT_PRESERVE_GPIO) {
        keep |= 1UL << TIKU_ESP32C61_LINE_GPIO;
    }
    if (preserve_mask & TIKU_CRIT_PRESERVE_UART) {
        keep |= 1UL << TIKU_ESP32C61_LINE_UART0;
    }
    crit_saved = tiku_esp32c61_irq_enabled();
    tiku_esp32c61_irq_set_enabled(crit_saved & keep);
}

void tiku_crit_arch_unmask_irqs(void) {
    tiku_esp32c61_irq_set_enabled(crit_saved);
}
