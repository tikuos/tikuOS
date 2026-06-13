/*
 * Tiku Operating System v0.05
 * Simple. Ubiquitous. Intelligence, Everywhere.
 * http://tiku-os.org
 *
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 *
 * tiku_wake_arch.c - Apollo 510 wake-source query (stub)
 *
 * Reports no active wake sources for now. A real implementation reads the
 * SysTick / NVIC / GPIO interrupt-enable state to populate the snapshot.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include <string.h>
#include <hal/tiku_wake_hal.h>

/**
 * @brief Query the active wake sources (stub — reports none)
 *
 * A real implementation reads the SysTick / NVIC / GPIO interrupt-enable
 * state to populate the snapshot. Until that lands the output is zeroed
 * so callers see a clean, empty wake-source set rather than garbage.
 *
 * @param out  Wake-source snapshot to populate (zeroed; must be non-NULL
 *             to avoid the guard check, but the guard is a no-op here)
 */
void tiku_wake_arch_query(tiku_wake_sources_t *out) {
    if (out) {
        memset(out, 0, sizeof(*out));
    }
}
