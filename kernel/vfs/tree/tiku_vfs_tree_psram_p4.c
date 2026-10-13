/*
 * Tiku Operating System v0.06
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 * tiku_vfs_tree_psram_p4.c - Passive status of boot-attached P4 PSRAM.
 * SPDX-License-Identifier: Apache-2.0
 */
#include "tiku_vfs_tree_psram.h"
#include <arch/esp32p4/tiku_psram_arch.h>
#include <kernel/memory/tiku_mem.h>
#include <stdio.h>

/** @brief Report whether the verified mapping still owns its allocator span. */
static int state_read(char *out, size_t size)
{
    const uint8_t *base;
    tiku_mem_stats_t stats;
    int attached = tiku_tier_span_stats(TIKU_MEM_PSRAM, 0, &base, &stats) == TIKU_MEM_OK &&
        (uintptr_t)base == TIKU_P4_PSRAM_BASE && stats.total_bytes == tiku_p4_psram_size();
    return snprintf(out, size, "%s\n", attached ? "up" :
                    tiku_p4_psram_size() ? "mapped" : "unavailable");
}
/** @brief Return verified capacity; reads never initialize the device. */
static int size_read(char *out, size_t size)
{
    return snprintf(out, size, "%lu\n", (unsigned long)tiku_p4_psram_size());
}
/** @brief Report the qualified interface clock, or zero when not initialized. */
static int hz_read(char *out, size_t size)
{
    return snprintf(out, size, "%lu\n", tiku_p4_psram_size() ? 80000000UL : 0UL);
}
/** @brief Report the last completed initialization stage without hardware writes. */
static int stage_read(char *out, size_t size)
{
    return snprintf(out, size, "%u\n", tiku_p4_psram_stage());
}
static const tiku_vfs_desc_t state_desc = {.vtype = TIKU_VFS_T_STR};
static const tiku_vfs_desc_t number_desc = {.vtype = TIKU_VFS_T_U32};
const tiku_vfs_node_t tiku_vfs_tree_psram_children[] = {
    {"state", TIKU_VFS_FILE, state_read, NULL, NULL, 0, &state_desc},
    {"size", TIKU_VFS_FILE, size_read, NULL, NULL, 0, &number_desc},
    {"hz", TIKU_VFS_FILE, hz_read, NULL, NULL, 0, &number_desc},
    {"stage", TIKU_VFS_FILE, stage_read, NULL, NULL, 0, &number_desc}
};
_Static_assert(sizeof tiku_vfs_tree_psram_children / sizeof tiku_vfs_tree_psram_children[0]
               == TIKU_VFS_TREE_PSRAM_NCHILD, "P4 PSRAM child count");
