/*
 * Tiku Operating System v0.06
 * Simple. Persistent. Intelligent.
 * http://tiku-os.org
 *
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 *
 * tiku_xip_arch.c - Validate paired SRAM metadata before flash execution.
 *
 * The SRAM image carries the expected 64-byte header of the flash code at
 * flash offset 0x100000; tiku_p4_xip_init() maps flash and requires an
 * exact header match and payload CRC.
 *
 * SPDX-License-Identifier: Apache-2.0
 */
#include "tiku_xip_arch.h"
#include "tiku_mmap_arch.h"
#include "tiku_flash_layout.h"
#include <kernel/memory/tiku_nvm_mirror.h>
#ifndef TIKU_P4_XIP_FENCE
#define TIKU_P4_XIP_FENCE() __asm__ volatile("fence.i" ::: "memory")
#endif

__attribute__((section(".p4.expected"), used, aligned(4)))
const uint32_t tiku_p4_xip_expected[16] = {0x554b4954u, 0x50583450u, 1};

int tiku_p4_xip_validate(const uint32_t *expected, const uint8_t *mapped,
                         size_t capacity)
{
    const volatile uint32_t *want = expected;
    const volatile uint32_t *got = (const volatile uint32_t *)mapped;
    uint32_t length, identity = 0;
    unsigned i;
    if (!expected || !mapped || capacity < 64 ||
        ((uintptr_t)expected & 3u) || ((uintptr_t)mapped & 3u)) return 0;
    if (want[0] != 0x554b4954u || want[1] != 0x50583450u || want[2] != 1) return 0;
    length = want[3];
    if (length <= 64 || length > capacity || (length & 3u) ||
        want[5] || want[6] || want[7]) return 0;
    for (i = 0; i < 16; i++) {
        if (want[i] != got[i]) return 0;
        if (i >= 8) identity |= want[i];
    }
    return identity && tiku_nvm_crc32(mapped + 64, length - 64) == want[4];
}

int tiku_p4_xip_init(void)
{
    if (tiku_p4_mmap_init() || !tiku_p4_xip_validate(tiku_p4_xip_expected,
        (const uint8_t *)(TIKU_P4_FLASH_VIRTUAL + TIKU_P4_FLASH_XIP_START),
        TIKU_P4_FLASH_XIP_END - TIKU_P4_FLASH_XIP_START)) return -1;
    TIKU_P4_XIP_FENCE();
    return 0;
}
