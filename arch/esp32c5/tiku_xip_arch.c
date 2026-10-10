/*
 * Tiku Operating System v0.06
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 * tiku_xip_arch.c - C5 pre-execution companion identity and payload check.
 * The packer fills the SRAM descriptor and matching flash header after linking.
 * SPDX-License-Identifier: Apache-2.0
 */
#include "tiku_xip_arch.h"
#include "tiku_flash_arch.h"
#include "tiku_timer_arch.h"
#include <kernel/memory/tiku_nvm_mirror.h>

#ifndef TIKU_C5_XIP_FENCE
#define TIKU_C5_XIP_FENCE() __asm__ volatile("fence.i" ::: "memory")
#endif

__attribute__((section(".c5.expected"), used))
const uint32_t tiku_c5_xip_expected[16] = {0x554B4954u, 0x50583543u, 1u};

int tiku_c5_xip_validate(const uint32_t *expected, const uint8_t *mapped,
                         size_t capacity)
{
    const volatile uint32_t *want = expected;
    const volatile uint32_t *header = (const volatile uint32_t *)mapped;
    uint32_t length, identity = 0;
    unsigned i;

    if (expected == NULL || mapped == NULL ||
        capacity < TIKU_C5_XIP_HEADER_BYTES || ((uintptr_t)expected & 3u) ||
        ((uintptr_t)mapped & 3u)) {
        return 0;
    }
    if (want[0] != 0x554B4954u || want[1] != 0x50583543u || want[2] != 1u) {
        return 0;
    }
    length = want[3];
    if (length <= TIKU_C5_XIP_HEADER_BYTES || length > capacity ||
        (length & 3u) || want[5] || want[6] || want[7]) {
        return 0;
    }
    for (i = 0; i < 16; i++) {
        if (want[i] != header[i]) {
            return 0;
        }
        if (i >= 8) {
            identity |= want[i];
        }
    }
    return identity != 0 &&
           tiku_nvm_crc32(mapped + TIKU_C5_XIP_HEADER_BYTES,
                          length - TIKU_C5_XIP_HEADER_BYTES) == want[4];
}

void tiku_c5_xip_require(void)
{
    if (!tiku_c5_xip_validate(tiku_c5_xip_expected,
                              tiku_flash_map(TIKU_FLASH_XIP_ADDR),
                              TIKU_FLASH_XIP_BYTES)) {
        tiku_c5_fatal("XIP companion missing, damaged or from another build");
    }
    TIKU_C5_XIP_FENCE();
}
