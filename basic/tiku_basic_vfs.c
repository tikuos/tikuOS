/* SPDX-License-Identifier: Apache-2.0 */
#include "tiku.h"
#include "tiku_basic.h"
#include "tiku_basic_vfs.h"

#if TIKU_SHELL_ENABLE && TIKU_SHELL_CMD_BASIC

/**
 * @brief Read handler for /data/basic, the saved BASIC program.
 *
 * Renders the BASIC interpreter's saved program as text.
 *
 * @param buf  Output buffer
 * @param max  Capacity of @p buf
 * @return Bytes written (see tiku_basic_vfs_read)
 */
static int
data_basic_read(char *buf, size_t max)
{
    return tiku_basic_vfs_read(buf, (unsigned int)max);
}

/**
 * @brief Write handler for /data/basic, the saved BASIC program.
 *
 * Saves @p len bytes of numbered program text as the BASIC program.
 *
 * @param buf  Program text
 * @param len  Number of bytes
 * @return 0 on success, negative on error (see tiku_basic_vfs_write)
 */
static int
data_basic_write(const char *buf, size_t len)
{
    return tiku_basic_vfs_write(buf, (unsigned int)len);
}

static const tiku_vfs_node_t basic_children[] = {
    { "basic", TIKU_VFS_FILE, data_basic_read, data_basic_write, NULL, 0,
      NULL, NULL, TIKU_VFS_CAP_FS },
};


/** @brief Return the saved-program node without reading its storage. */
const tiku_vfs_node_t *tiku_basic_vfs_get(void)
{
    return basic_children;
}
#endif
