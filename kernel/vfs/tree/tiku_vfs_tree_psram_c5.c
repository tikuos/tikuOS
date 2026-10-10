/*
 * Tiku Operating System v0.06
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 * tiku_vfs_tree_psram_c5.c - passive C5 PSRAM status and explicit tier
 * lifecycle. SPDX-License-Identifier: Apache-2.0
 */
#include "tiku_vfs_tree_psram.h"
#include <arch/esp32c5/tiku_psram_arch.h>
#include <arch/esp32c5/tiku_cpu_common.h>
#include <stdio.h>
#include <string.h>

/** @brief Report attachment, verified mapping, absence or the last failed
 * operation. */
static int state_read(char *out, size_t size)
{
    const char *state = tiku_c5_psram_attached() ? "up"
                        : tiku_c5_psram_size()   ? "mapped"
                        : tiku_c5_psram_result() == TIKU_C5_PSRAM_ABSENT
                            ? "absent"
                        : tiku_c5_psram_result() ? "error"
                                                 : "down";
    return snprintf(out, size, "%s\n", state);
}
/** @brief Accept up or down with optional trailing whitespace; refuse live
 * allocations. */
static int state_write(const char *text, size_t size)
{
    int rc;
    while (size && (text[size - 1] == ' ' || text[size - 1] == '\t' ||
                    text[size - 1] == '\r' || text[size - 1] == '\n')) {
        size--;
    }
    if (size == 2 && !memcmp(text, "up", 2)) {
        rc = tiku_c5_psram_attach();
    } else if (size == 4 && !memcmp(text, "down", 4)) {
        rc = tiku_c5_psram_down();
    } else {
        return TIKU_VFS_EINVAL;
    }
    if (rc == 0) {
        return 0;
    }
    if (rc == TIKU_C5_PSRAM_BUSY) {
        return TIKU_VFS_EBUSY;
    }
    if (rc == TIKU_C5_PSRAM_ABSENT || rc == TIKU_C5_PSRAM_ID ||
        rc == TIKU_C5_PSRAM_CONFIG) {
        return TIKU_VFS_ENOTSUP;
    }
    return TIKU_VFS_EIO;
}
/** @brief Read verified mapped capacity, not package-advertised capacity. */
static int size_read(char *out, size_t size)
{
    return snprintf(out, size, "%lu\n", (unsigned long)tiku_c5_psram_size());
}
/** @brief Read the last SPI identity without probing a device. */
static int id_read(char *out, size_t size)
{
    return snprintf(out, size, "%06lx\n", (unsigned long)tiku_c5_psram_id());
}
/** @brief Read the package's raw PSRAM code without claiming external capacity.
 */
static int package_read(char *out, size_t size)
{
    return snprintf(out, size, "%u\n", tiku_cpu_c5_psram_package_code());
}
/** @brief Read the last lifecycle result without changing hardware. */
static int result_read(char *out, size_t size)
{
    return snprintf(out, size, "%d\n", tiku_c5_psram_result());
}
static const tiku_vfs_desc_t state_desc = {.vtype = TIKU_VFS_T_STR};
static const tiku_vfs_desc_t number_desc = {.vtype = TIKU_VFS_T_U32};
static const tiku_vfs_desc_t result_desc = {.vtype = TIKU_VFS_T_I32};
const tiku_vfs_node_t tiku_vfs_tree_psram_children[] = {
    {"state", TIKU_VFS_FILE, state_read, state_write, NULL, 0, &state_desc,
     NULL, TIKU_VFS_CAP_SYS},
    {"size", TIKU_VFS_FILE, size_read, NULL, NULL, 0, &number_desc, NULL, 0},
    {"id", TIKU_VFS_FILE, id_read, NULL, NULL, 0, &state_desc, NULL, 0},
    {"package_code", TIKU_VFS_FILE, package_read, NULL, NULL, 0, &number_desc,
     NULL, 0},
    {"result", TIKU_VFS_FILE, result_read, NULL, NULL, 0, &result_desc, NULL,
     0}};
_Static_assert(sizeof(tiku_vfs_tree_psram_children) /
                       sizeof(tiku_vfs_tree_psram_children[0]) ==
                   TIKU_VFS_TREE_PSRAM_NCHILD,
               "C5 PSRAM child count");
