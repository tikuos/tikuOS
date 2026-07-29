/*
 * Tiku Operating System v0.06
 * Simple. Ubiquitous. Intelligence, Everywhere.
 * http://tiku-os.org
 *
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 *
 * tiku_basic_vfs_file.inl - VFS bridge for /data/basic.
 *
 * Lets a read and write of /data/basic round-trip the saved program text through
 * the same durable store SAVE and LOAD use.  Its two entry points are the only
 * non-static symbols here.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

/*---------------------------------------------------------------------------*/
/* /data/basic VFS HANDLERS                                                  */
/*---------------------------------------------------------------------------*/

/**
 * @brief Read handler for the /data/basic VFS node.
 *
 * Mirrors tiku_persist_read into the caller's buffer.
 *
 * @param buf  Destination buffer.
 * @param max  Capacity of @p buf in bytes.
 *
 * @return Number of bytes written (0 on no saved program), -1 on
 *         error.
 */
int
tiku_basic_vfs_read(char *buf, unsigned int max)
{
    size_t n_read = 0;

    if (buf == NULL || max == 0u) {
        return -1;
    }
    /* Same default-slot storage as SAVE/LOAD (NVM region on Ambiq). */
    if (basic_prog_fetch(buf, (size_t)max, &n_read) != 0) {
        buf[0] = '\0';      /* no saved program */
        return 0;
    }
    return (int)n_read;
}

/**
 * @brief Write handler for the /data/basic VFS node.
 *
 * Mirrors tiku_persist_write under MPU bracketing.  The caller-supplied data is
 * taken verbatim -- the user is responsible for sending text LOAD can parse,
 * meaning numbered lines separated by '\n'.
 *
 * @param buf  Source buffer.
 * @param len  Number of bytes to write.
 *
 * @return 0 on success, -1 on error.
 */
int
tiku_basic_vfs_write(const char *buf, unsigned int len)
{
    if (buf == NULL || len > TIKU_BASIC_SAVE_BUF_BYTES) {
        return -1;
    }
    /* Same default-slot storage as SAVE/LOAD (NVM region on Ambiq). */
    return basic_prog_store(buf, (size_t)len);
}
