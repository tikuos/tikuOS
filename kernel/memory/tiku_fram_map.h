/*
 * Tiku Operating System
 * http://tiku-os.org
 *
 * tiku_fram_map.h - Backward-compatibility shim
 *
 * This header has been renamed to tiku_nvm_map.h.  All types,
 * constants, and functions are now tiku_nvm_* instead of tiku_fram_*.
 * This file simply includes the new header, which provides
 * backward-compatible #define aliases for all old names.
 *
 * New code should #include <kernel/memory/tiku_nvm_map.h> directly.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#ifndef TIKU_FRAM_MAP_H_
#define TIKU_FRAM_MAP_H_

#include "tiku_nvm_map.h"

#endif /* TIKU_FRAM_MAP_H_ */
