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

void tiku_wake_arch_query(tiku_wake_sources_t *out) {
    if (out) {
        memset(out, 0, sizeof(*out));
    }
}
