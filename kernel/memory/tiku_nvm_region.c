/*
 * Tiku Operating System v0.06
 * Simple. Ubiquitous. Intelligence, Everywhere.
 * http://tiku-os.org
 *
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 *
 * tiku_nvm_region.c - weak default for the carved NVM region accessor.
 *
 * Boards with a carved region provide a strong tiku_nvm_backend_get() in their
 * arch backend.  This weak default returns NULL so the accessor still links where
 * there is no region, giving "no region" instead of an undefined symbol.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include "tiku_nvm_region.h"

__attribute__((weak))
const tiku_nvm_backend_t *tiku_nvm_backend_get(void)
{
    return NULL;
}
