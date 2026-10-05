/*
 * Tiku Operating System v0.06
 * Simple. Ubiquitous. Intelligence, Everywhere.
 * http://tiku-os.org
 *
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 *
 * tiku_nvm_region.c - weak default for the carved NVM region accessor.
 *
 * This weak tiku_nvm_backend_get() returns NULL, which callers take as no
 * carved region.  A board with a region overrides it with a strong definition
 * in its arch backend.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include "tiku_nvm_region.h"

__attribute__((weak))
const tiku_nvm_backend_t *tiku_nvm_backend_get(void)
{
    return NULL;
}
