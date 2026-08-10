/*
 * Tiku Operating System v0.06
 * Simple. Ubiquitous. Intelligence, Everywhere.
 * http://tiku-os.org
 *
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 *
 * tiku_shell_cmd_nvmprobe.c - "nvmprobe" diagnostic for the carved NVM region.
 *
 * An opt-in affordance to exercise the memory-mapped region backend from the
 * shell and the bench suite: report its geometry, and read or write at an offset.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include "tiku_shell_cmd_nvmprobe.h"
#include <kernel/shell/tiku_shell.h>
#include "kernel/memory/tiku_nvm_region.h"
#include <kernel/memory/tiku_mem.h>      /* tiku_mpu_unlock_nvm / lock_nvm */

#include <stdint.h>
#include <stdlib.h>
#include <string.h>

#define NVMPROBE_READ_MAX  64u

/** @brief Lowercase hex digit for the low nibble of @p v. */
static char
nvmprobe_hex(unsigned v)
{
    v &= 0xfu;
    return (v < 10u) ? (char)('0' + v) : (char)('a' + (v - 10u));
}

void
tiku_shell_cmd_nvmprobe(uint8_t argc, const char *argv[])
{
    const tiku_nvm_backend_t *be = tiku_nvm_backend_get();
    const char *sub = (argc > 1u) ? argv[1] : "info";

    if (be == NULL || be->base == NULL || be->size == 0u) {
        SHELL_PRINTF("nvmprobe: no NVM region on this board\n");
        return;
    }

    if (strcmp(sub, "info") == 0) {
        SHELL_PRINTF("nvmprobe: base=0x%lx size=0x%lx\n",
                     (unsigned long)(uintptr_t)be->base,
                     (unsigned long)be->size);
        return;
    }

    if (strcmp(sub, "read") == 0 && argc >= 4u) {
        unsigned long off = strtoul(argv[2], NULL, 0);
        unsigned long len = strtoul(argv[3], NULL, 0);
        unsigned long i;

        if (len > NVMPROBE_READ_MAX) {
            len = NVMPROBE_READ_MAX;
        }
        if (off >= be->size || len > (unsigned long)be->size - off) {
            SHELL_PRINTF("nvmprobe: range out of bounds\n");
            return;
        }
        SHELL_PRINTF("nvmprobe: @0x%lx text=\"", off);
        for (i = 0; i < len; i++) {
            unsigned uc = be->base[off + i];
            SHELL_PRINTF("%c", (uc >= 0x20u && uc < 0x7fu) ? (char)uc : '.');
        }
        SHELL_PRINTF("\" hex=");
        for (i = 0; i < len; i++) {
            unsigned uc = be->base[off + i];
            SHELL_PRINTF("%c%c", nvmprobe_hex(uc >> 4), nvmprobe_hex(uc));
        }
        SHELL_PRINTF("\n");
        return;
    }

    if (strcmp(sub, "write") == 0 && argc >= 4u) {
        unsigned long off = strtoul(argv[2], NULL, 0);
        const char   *txt = argv[3];
        unsigned long len = (unsigned long)strlen(txt);
        uint16_t mpu;
        int rc;

        if (be->write == NULL) {
            SHELL_PRINTF("nvmprobe: region is read-only\n");
            return;
        }
        if (off >= be->size || len > (unsigned long)be->size - off) {
            SHELL_PRINTF("nvmprobe: range out of bounds\n");
            return;
        }
        mpu = tiku_mpu_unlock_nvm();
        rc  = be->write((tiku_nvm_backend_t *)be, (size_t)off, txt, (size_t)len);
        tiku_mpu_lock_nvm(mpu);
        SHELL_PRINTF("nvmprobe: wrote %lu @0x%lx rc=%d\n", len, off, rc);
        return;
    }

    if (strcmp(sub, "verify") == 0 && argc >= 4u) {
        unsigned long off = strtoul(argv[2], NULL, 0);
        const char   *txt = argv[3];
        unsigned long len = (unsigned long)strlen(txt);
        int ok;

        if (off >= be->size || len > (unsigned long)be->size - off) {
            SHELL_PRINTF("nvmprobe: range out of bounds\n");
            return;
        }
        ok = (memcmp(be->base + off, txt, (size_t)len) == 0);
        SHELL_PRINTF("nvmprobe: verify @0x%lx %s\n", off, ok ? "PASS" : "FAIL");
        return;
    }

    if (strcmp(sub, "tier") == 0) {
        /*
         * NVM-tier self-test: allocate from the tier, write through
         * tiku_tier_nvm_write(), verify by plain readback, then confirm an
         * over-capacity arena is refused.  The tier has no free, so each run
         * consumes one 256 B arena until reboot.  "tier mark <txt>" writes
         * <txt> instead of the fixed pattern and prints the block's region
         * offset, so the bench can re-verify the bytes after a reset through
         * the raw read path.
         */
        const char *txt = (argc >= 4u && strcmp(argv[2], "mark") == 0)
                          ? argv[3] : "TIER-SELFTEST";
        size_t len = strlen(txt);
        tiku_mem_stats_t st0, st1;
        tiku_arena_t ar, over;
        uint8_t *blk;
        int wr_ok, vf_ok, rf_ok;

        if (len > 63u) {
            len = 63u;
        }
        (void)tiku_tier_init();
        if (tiku_tier_stats(TIKU_MEM_NVM, &st0) != TIKU_MEM_OK) {
            SHELL_PRINTF("nvmprobe: tier not available\n");
            return;
        }
        if (tiku_tier_arena_create(&ar, TIKU_MEM_NVM, 256u, 141u)
            != TIKU_MEM_OK) {
            SHELL_PRINTF("nvmprobe: tier arena create failed\n");
            return;
        }
        blk = tiku_arena_alloc(&ar, 64u);
        if (blk == NULL) {
            SHELL_PRINTF("nvmprobe: tier arena alloc failed\n");
            return;
        }
        wr_ok = (tiku_tier_nvm_write(blk, txt,
                                     (tiku_mem_arch_size_t)len)
                 == TIKU_MEM_OK);
        vf_ok = (memcmp(blk, txt, len) == 0);
        rf_ok = (tiku_tier_arena_create(&over, TIKU_MEM_NVM,
                                        st0.total_bytes + 64u, 142u)
                 != TIKU_MEM_OK);
        (void)tiku_tier_stats(TIKU_MEM_NVM, &st1);
        SHELL_PRINTF("nvmprobe: tier total=%lu used=%lu->%lu "
                     "write=%s verify=%s refuse=%s\n",
                     (unsigned long)st1.total_bytes,
                     (unsigned long)st0.used_bytes,
                     (unsigned long)st1.used_bytes,
                     wr_ok ? "PASS" : "FAIL",
                     vf_ok ? "PASS" : "FAIL",
                     rf_ok ? "PASS" : "FAIL");
        if (argc >= 4u && strcmp(argv[2], "mark") == 0) {
            if (blk >= be->base && blk < be->base + be->size) {
                SHELL_PRINTF("nvmprobe: tier mark @0x%lx len=%lu\n",
                             (unsigned long)(blk - be->base),
                             (unsigned long)len);
            } else {
                SHELL_PRINTF("nvmprobe: tier mark unmapped\n");
            }
        }
        return;
    }

    SHELL_PRINTF("usage: nvmprobe [info | read <off> <len> | "
                 "write <off> <txt> | verify <off> <txt> | "
                 "tier [mark <txt>]]\n");
}
