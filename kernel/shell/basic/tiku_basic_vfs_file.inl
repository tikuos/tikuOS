/*
 * Tiku Operating System
 * http://tiku-os.org
 *
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 *
 * tiku_basic_vfs_file.inl - VFS bridge for /data/basic.
 *
 * NOT a standalone translation unit.  Included from tiku_basic.c.
 *
 * Lets `read /data/basic` and `write /data/basic` round-trip the
 * saved BASIC program text through the same NVM-backed persist
 * store used by SAVE / LOAD.  These two functions are the only
 * non-static symbols defined in this piece -- they are declared in
 * tiku_basic.h.
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at:
 *
 *     http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or
 * implied.  See the License for the specific language governing
 * permissions and limitations under the License.
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
    tiku_mem_arch_size_t n_read = 0;
    tiku_mem_err_t       rc;

    if (buf == NULL || max == 0u) {
        return -1;
    }
    if (basic_persist_ensure() != 0) {
        return -1;
    }

    rc = tiku_persist_read(&basic_store, BASIC_PERSIST_KEY,
            (uint8_t *)buf, (tiku_mem_arch_size_t)max, &n_read);
    if (rc == TIKU_MEM_ERR_NOT_FOUND) {
        if (max > 0u) {
            buf[0] = '\0';
        }
        return 0;
    }
    if (rc != TIKU_MEM_OK) {
        return -1;
    }
    return (int)n_read;
}

/**
 * @brief Write handler for the /data/basic VFS node.
 *
 * Mirrors tiku_persist_write under MPU bracketing.  The caller-
 * supplied data is taken verbatim -- the user is responsible for
 * sending text that LOAD can parse (numbered lines, '\n'
 * separated).
 *
 * @param buf  Source buffer.
 * @param len  Number of bytes to write.
 *
 * @return 0 on success, -1 on error.
 */
int
tiku_basic_vfs_write(const char *buf, unsigned int len)
{
    uint16_t       mpu;
    tiku_mem_err_t rc;

    if (buf == NULL) {
        return -1;
    }
    if (basic_persist_ensure() != 0) {
        return -1;
    }
    if (len > TIKU_BASIC_SAVE_BUF_BYTES) {
        return -1;
    }

    mpu = tiku_mpu_unlock_nvm();
    rc = tiku_persist_write(&basic_store, BASIC_PERSIST_KEY,
            (const uint8_t *)buf, (tiku_mem_arch_size_t)len);
    tiku_mpu_lock_nvm(mpu);
    return (rc == TIKU_MEM_OK) ? 0 : -1;
}
