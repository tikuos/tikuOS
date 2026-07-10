/*
 * Tiku Operating System v0.05
 * Simple. Ubiquitous. Intelligence, Everywhere.
 * http://tiku-os.org
 *
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 *
 * tiku_nvm_region.c - weak default for the carved NVM region accessor.
 *
 * Boards with a carved, memory-mapped NVM region provide a STRONG
 * tiku_nvm_backend_get() in their arch backend (e.g.
 * arch/ambiq/tiku_nvm_region_apollo4l.c). This weak default returns NULL so the
 * accessor still links on parts that have no region yet (host, RP2350) -- a
 * consumer just sees "no region" rather than an undefined-symbol link error.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include "tiku_nvm_region.h"

__attribute__((weak))
const tiku_nvm_backend_t *tiku_nvm_backend_get(void)
{
    return NULL;
}
