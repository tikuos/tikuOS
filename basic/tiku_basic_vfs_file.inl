/*
 * Tiku Operating System v0.06
 * Simple. Ubiquitous. Intelligence, Everywhere.
 * http://tiku-os.org
 *
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 *
 * tiku_basic_vfs_file.inl - VFS bridge for /data/basic.
 *
 * Lets a read and write of /data/basic round-trip the saved program text
 * through the same durable slot SAVE and LOAD use.  Its two entry points are
 * the only non-static symbols here.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

/*---------------------------------------------------------------------------*/
/* /data/basic VFS HANDLERS                                                  */
/*---------------------------------------------------------------------------*/

int
tiku_basic_vfs_read(char *buf, unsigned int max)
{
    size_t n_read = 0;

    if (buf == NULL || max == 0u) {
        return -1;
    }
    /* Same default slot as SAVE/LOAD: prog.bas, or the MSP430/host store. */
    if (basic_prog_fetch(buf, (size_t)max, &n_read) != 0) {
        buf[0] = '\0';      /* none saved, or it does not fit max */
        return 0;
    }
    return (int)n_read;
}

int
tiku_basic_vfs_write(const char *buf, unsigned int len)
{
    if (buf == NULL || len > TIKU_BASIC_SAVE_BUF_BYTES) {
        return -1;
    }
    /* Stored verbatim, in the slot SAVE and LOAD use. */
    return basic_prog_store(buf, (size_t)len);
}
