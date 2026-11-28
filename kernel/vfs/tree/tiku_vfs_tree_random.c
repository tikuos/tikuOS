/*
 * Tiku Operating System v0.06
 * Simple. Persistent. Intelligent.
 * http://tiku-os.org
 *
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 *
 * tiku_vfs_tree_random.c - Expose hardware entropy through VFS character nodes.
 *
 * SPDX-License-Identifier: Apache-2.0
 */
#include "tiku_vfs_tree_random.h"
#include <stdio.h>
#ifdef TIKU_VFS_TRNG_HEADER
#include TIKU_VFS_TRNG_HEADER

/** @brief Read one word for the text interface. */
int tiku_vfs_random_text(char *buf, size_t max)
{
    uint32_t word;

    if (tiku_trng_arch_read_u32(&word) != TIKU_TRNG_OK) return TIKU_VFS_EIO;
    return snprintf(buf, max, "%08lx\n", (unsigned long)word);
}

/** @brief One sample of up to 64 bytes per kernel call. */
static int random_bytes(uint32_t offset, char *buf, size_t max, uint32_t *total)
{
    size_t n = max < 64u ? max : 64u;

    if (total != NULL) *total = 0;
    if (offset != 0) return 0;
    if (tiku_trng_arch_read_bytes((uint8_t *)buf, n) != TIKU_TRNG_OK)
        return TIKU_VFS_EIO;
    if (total != NULL) *total = (uint32_t)n;
    return (int)n;
}

const tiku_vfs_desc_t tiku_vfs_random_desc = {
    .fresh = TIKU_VFS_FRESH_LIVE, .ecost = TIKU_VFS_E_PERIPH,
    .flags = TIKU_VFS_DF_CHAR | TIKU_VFS_DF_READ_CONSUMES, .read_at = random_bytes
};
#endif
