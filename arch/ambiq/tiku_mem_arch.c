/*
 * Tiku Operating System v0.05
 * Simple. Ubiquitous. Intelligence, Everywhere.
 * http://tiku-os.org
 *
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 *
 * tiku_mem_arch.c - Apollo 510 memory architecture support
 *
 * At this milestone persistent state lives in the SRAM .uninit region
 * (warm-reset durable, reseeded on power cycle — same as RP2350 today),
 * so NVM read/write are plain copies and flush is a no-op. A later pass
 * mirrors .uninit to an MRAM page via am_hal_mram (with D-cache
 * maintenance) for full power-cycle durability.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include <string.h>
#include "tiku_mem_arch.h"

void tiku_mem_arch_init(void) {
    /* Caches are enabled in tiku_cpu_boot_ambiq_init(); nothing else to
     * do here at this milestone. */
}

void tiku_mem_arch_secure_wipe(uint8_t *buf, tiku_mem_arch_size_t len) {
    volatile uint8_t *p = buf;
    while (len--) {
        *p++ = 0u;
    }
}

void tiku_mem_arch_nvm_read(uint8_t *dst, const uint8_t *src,
                            tiku_mem_arch_size_t len) {
    /* .uninit NVM stand-in is memory-mapped SRAM — a plain copy. */
    memcpy(dst, src, len);
}

void tiku_mem_arch_nvm_write(uint8_t *dst, const uint8_t *src,
                             tiku_mem_arch_size_t len) {
    memcpy(dst, src, len);
}

void tiku_mem_arch_nvm_flush(void) {
    /* No MRAM mirror yet — .uninit already holds the live copy. */
}
