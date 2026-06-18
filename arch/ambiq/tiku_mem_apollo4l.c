/*
 * Tiku Operating System v0.05
 * Simple. Ubiquitous. Intelligence, Everywhere.
 * http://tiku-os.org
 *
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 *
 * tiku_mem_arch_apollo4l.c - Apollo4 Lite memory architecture + NVM
 *
 * Mirrors arch/ambiq/tiku_mem_arch.c (Apollo510), but:
 *   - the Cortex-M4 core has NO SCB L1 cache, so no D-cache maintenance is
 *     needed (the M55 driver cleans/invalidates around the MRAM page);
 *   - the power-cycle MRAM mirror is DEFERRED: the Apollo4 bootrom MRAM
 *     programmer lives at a different helper address and uses a different
 *     program key than Apollo5, so tiku_mem_arch_nvm_flush() is a stub for now
 *     (an "apollo4l mem port C" milestone). Until then, persistent state is
 *     warm-reset durable (the .uninit NOLOAD region) but not power-cycle
 *     durable -- the same state Apollo510 shipped in before its mem-port-C.
 *
 * The restore path is kept (it reads the reserved MRAM page and is a harmless
 * no-op while the magic is never written), so it works as soon as the flush lands.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include <string.h>
#include <stdint.h>
#include "tiku_mem_arch.h"

/* Live .uninit working copy + the reserved MRAM mirror page (apollo4l.ld). */
extern uint8_t  __uninit_start;
extern uint8_t  __uninit_end;
extern uint32_t __tiku_nvm_mram_start;   /* base address of the mirror page */

#define TIKU_NVM_MAGIC          0x4E564D54U   /* 'NVMT' little-endian */
#define TIKU_NVM_MRAM_BYTES     0x8000U       /* MUST match __tiku_nvm_mram_size */

/** @brief Return the size of the .uninit region in bytes. */
static size_t uninit_bytes(void) {
    return (size_t)((uintptr_t)&__uninit_end - (uintptr_t)&__uninit_start);
}

/**
 * @brief Restore .uninit from the MRAM mirror on boot (if the magic matches).
 *
 * A harmless no-op until tiku_mem_arch_nvm_flush() programs the mirror; kept so
 * power-cycle restore works as soon as the Apollo4 MRAM programmer is wired up.
 */
void tiku_mem_arch_init(void) {
    const uint32_t *mirror = (const uint32_t *)&__tiku_nvm_mram_start;
    if (mirror[0] == TIKU_NVM_MAGIC) {
        size_t n = uninit_bytes();
        if (n > (TIKU_NVM_MRAM_BYTES - 4U)) {
            n = TIKU_NVM_MRAM_BYTES - 4U;
        }
        memcpy(&__uninit_start, (const uint8_t *)&mirror[1], n);
    }
}

/** @brief Zero-fill a buffer via a volatile pointer (defeats optimization). */
void tiku_mem_arch_secure_wipe(uint8_t *buf, tiku_mem_arch_size_t len) {
    volatile uint8_t *p = buf;
    while (len--) {
        *p++ = 0u;
    }
}

/** @brief Copy from the .uninit NVM region (memory-mapped SRAM) into a buffer. */
void tiku_mem_arch_nvm_read(uint8_t *dst, const uint8_t *src,
                            tiku_mem_arch_size_t len) {
    memcpy(dst, src, len);
}

/** @brief Write into the .uninit SRAM working copy. */
void tiku_mem_arch_nvm_write(uint8_t *dst, const uint8_t *src,
                             tiku_mem_arch_size_t len) {
    memcpy(dst, src, len);
}

/**
 * @brief Snapshot .uninit to the MRAM mirror (stub -- power-cycle persistence
 *        deferred to an apollo4l mem-port-C milestone).
 *
 * Apollo4's bootrom MRAM programmer (helper-table address + program key) differs
 * from Apollo5's; wiring it (plus the magic header + 16-byte-aligned staging) is
 * a follow-up. Until then NVM state is warm-reset durable but not power-cycle
 * durable.
 */
void tiku_mem_arch_nvm_flush(void) {
    /* TODO (apollo4l mem port C): program the reserved MRAM page via the Apollo4
     * bootrom MRAM programmer. No-op for now. */
}
