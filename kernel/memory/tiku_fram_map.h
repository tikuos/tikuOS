/*
 * Tiku Operating System v0.06
 * Simple. Ubiquitous. Intelligence, Everywhere.
 * http://tiku-os.org
 *
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 *
 * tiku_fram_map.h - backward-compatibility shim for tiku_nvm_map.h.
 *
 * Every tiku_fram_* name is now tiku_nvm_*.  This header just includes the new
 * one, which defines aliases for the old names.  New code should include
 * kernel/memory/tiku_nvm_map.h directly.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#ifndef TIKU_FRAM_MAP_H_
#define TIKU_FRAM_MAP_H_

#include "tiku_nvm_map.h"

#endif /* TIKU_FRAM_MAP_H_ */
