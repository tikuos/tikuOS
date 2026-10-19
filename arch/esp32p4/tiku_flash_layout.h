/*
 * Tiku Operating System v0.06
 * Simple. Persistent. Intelligent.
 * http://tiku-os.org
 *
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 *
 * tiku_flash_layout.h - P4 physical flash ranges from flash_layout.def.
 *
 * Each TIKU_P4_FLASH_RANGE(name, start, end) line of flash_layout.def
 * becomes TIKU_P4_FLASH_<name>_START and _END, the end exclusive;
 * tools/esp32p4_layout.py parses the same file.
 *
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
