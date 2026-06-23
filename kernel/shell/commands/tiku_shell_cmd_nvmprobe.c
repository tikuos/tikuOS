/*
 * Tiku Operating System
 * http://tiku-os.org
 *
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 *
 * tiku_shell_cmd_nvmprobe.c - "nvmprobe" diagnostic for the carved NVM region.
 *
 * A thin, opt-in affordance to exercise the memory-mapped NVM region backend
 * (substrate B, tiku_nvm_region_get) from the shell and the TikuBench suite:
 *
 *   nvmprobe [info]             region base + size
 *   nvmprobe read   <off> <len> dump <len> bytes at <off> (text + hex)
 *   nvmprobe write  <off> <txt> program <txt> at <off> via the backend
 *   nvmprobe verify <off> <txt> read back and compare -> PASS / FAIL
 *
 * Reads are pointer derefs into the region; writes go through be->write inside a
 * tiku_mpu_unlock_nvm()/lock_nvm() window (MRAM bootrom program / FRAM store).
 * Direct-MRAM power-cycle durability proof: write -> reset/power-cycle ->
 * verify. Gated by TIKU_SHELL_CMD_NVMPROBE (off by default; enable via
 * EXTRA_CFLAGS="-DTIKU_SHELL_CMD_NVMPROBE=1").
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
    const tiku_nvm_backend_t *be = tiku_nvm_region_get();
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

    SHELL_PRINTF("usage: nvmprobe [info | read <off> <len> | "
                 "write <off> <txt> | verify <off> <txt>]\n");
}
