/*
 * Tiku Operating System v0.06
 * Simple. Persistent. Intelligent.
 * http://tiku-os.org
 *
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 *
 * tiku_cache_arch.c - Fixed L2 geometry through the P4 ECO2 ROM API.
 *
 * tiku_p4_cache_boot_init() has the ECO2 ROM set a 128 KiB L2 with 64-byte
 * lines, invalidates the cache and checks the geometry registers; it keeps
 * the L2 size register value it replaced.
 *
 * SPDX-License-Identifier: Apache-2.0
 */
#include "tiku_cache_arch.h"
#include "tiku_esp32p4_regs.h"

#ifndef TIKU_P4_CACHE_SET_MODE
#define TIKU_P4_CACHE_SET_MODE(size, ways, line) \
    ((void (*)(unsigned, unsigned, unsigned))0x4fc003d4u)(size, ways, line)
#define TIKU_P4_CACHE_INVALIDATE(map) ((int (*)(unsigned))0x4fc00404u)(map)
#endif
static unsigned initial_size;

unsigned tiku_p4_cache_initial_size(void) { return initial_size; }

int tiku_p4_cache_layout_ok(void)
{
    return (TIKU_P4_REG_READ(TIKU_P4_L2_SIZE) & 0x1fffu) == (1u << 9) &&
           (TIKU_P4_REG_READ(TIKU_P4_L2_LINE) & 0xffu) == (1u << 3);
}

int tiku_p4_cache_boot_init(void)
{
    if (TIKU_P4_REG_READ(TIKU_P4_ROM_CHIP_ID) != 18 ||
        TIKU_P4_REG_READ(TIKU_P4_ROM_ECO) != 2) return -1;
    initial_size = TIKU_P4_REG_READ(TIKU_P4_L2_SIZE);
    TIKU_P4_CACHE_SET_MODE(9, 8, 3);
    if (TIKU_P4_CACHE_INVALIDATE(1u << 5) != 0) return -1;
    return tiku_p4_cache_layout_ok() ? 0 : -1;
}
