/*
 * Tiku Operating System v0.06
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 * tiku_flash_layout.h - P4 physical layout constants from the range definition.
 * SPDX-License-Identifier: Apache-2.0
 */
#ifndef TIKU_P4_FLASH_LAYOUT_H_
#define TIKU_P4_FLASH_LAYOUT_H_
#define TIKU_P4_FLASH_RANGE(name, start, end) \
    TIKU_P4_FLASH_##name##_START = start, TIKU_P4_FLASH_##name##_END = end,
enum {
#include "flash_layout.def"
};
#undef TIKU_P4_FLASH_RANGE
#endif
