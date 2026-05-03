/*
 * Tiku Operating System
 * http://tiku-os.org
 *
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 *
 * tiku_basic_persist.inl - default-slot SAVE / LOAD via NVM store.
 *
 * NOT a standalone translation unit.  Included from tiku_basic.c.
 *
 * The default unnamed `SAVE` and `LOAD` go through basic_save_buf
 * registered under BASIC_PERSIST_KEY ("prog") in the FRAM-backed
 * tiku_persist store.  Multi-slot named SAVE / LOAD live in
 * tiku_basic_named_slots.inl.
 *
 * Implementation is lazy: basic_persist_ensure() registers the save
 * buffer with the persist store on first use; subsequent SAVE / LOAD
 * calls just read / write through the bracketed MPU unlock.
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
/* FORWARD DECLARATIONS                                                      */
/*---------------------------------------------------------------------------*/

/* Program-table helpers and the REPL line dispatcher are defined
 * further down in the orchestrator. */
static void prog_clear(void);
static int  prog_next_index(uint16_t lineno);
static void process_line(const char *raw);

/*---------------------------------------------------------------------------*/
/* PERSIST STORE BRING-UP                                                    */
/*---------------------------------------------------------------------------*/

/**
 * @brief Lazily register the save buffer with the persist store.
 *
 * Subsequent calls are O(1).  MPU is unlocked only for the metadata
 * mutation; data writes bracket their own MPU unlocks below.
 *
 * @return 0 on success, -1 on persist-store failure.
 */
static int
basic_persist_ensure(void)
{
    uint16_t       mpu;
    tiku_mem_err_t rc1, rc2;

    if (basic_persist_ready) {
        return 0;
    }
    mpu = tiku_mpu_unlock_nvm();
    rc1 = tiku_persist_init(&basic_store);
    rc2 = tiku_persist_register(&basic_store, BASIC_PERSIST_KEY,
            basic_save_buf, TIKU_BASIC_SAVE_BUF_BYTES);
    tiku_mpu_lock_nvm(mpu);
    if (rc1 != TIKU_MEM_OK || rc2 != TIKU_MEM_OK) {
        return -1;
    }
    basic_persist_ready = 1;
    return 0;
}

/*---------------------------------------------------------------------------*/
/* SAVE / LOAD                                                               */
/*---------------------------------------------------------------------------*/

/**
 * @brief Serialise the in-memory program in ascending order and
 *        commit it to FRAM under BASIC_PERSIST_KEY.
 *
 * @return 0 on success, -1 on persist failure or buffer overflow.
 */
static int
basic_save_to_persist(void)
{
    /* Serialize ascending-ordered program lines into a stack buffer
     * the same shape the REPL produces via LIST.  The buffer is
     * then copied to FRAM in one persist_write call. */
    static char    tmp[TIKU_BASIC_SAVE_BUF_BYTES];
    size_t         pos = 0;
    uint16_t       cur = 0;
    uint16_t       mpu;
    tiku_mem_err_t rc;

    if (basic_persist_ensure() != 0) {
        SHELL_PRINTF(SH_RED "? save: persist init failed\n" SH_RST);
        return -1;
    }

    while (1) {
        int idx = prog_next_index(cur);
        int n;
        if (idx < 0) {
            break;
        }
        n = snprintf(tmp + pos, sizeof(tmp) - pos, "%u %s\n",
                     (unsigned)prog[idx].number, prog[idx].text);
        if (n < 0 || (size_t)n >= sizeof(tmp) - pos) {
            SHELL_PRINTF(SH_RED
                         "? save: program too large for buffer\n" SH_RST);
            return -1;
        }
        pos += (size_t)n;
        if (prog[idx].number == 0xFFFFu) {
            break;
        }
        cur = (uint16_t)(prog[idx].number + 1);
    }

    mpu = tiku_mpu_unlock_nvm();
    rc = tiku_persist_write(&basic_store, BASIC_PERSIST_KEY,
            (const uint8_t *)tmp, (tiku_mem_arch_size_t)pos);
    tiku_mpu_lock_nvm(mpu);
    if (rc != TIKU_MEM_OK) {
        SHELL_PRINTF(SH_RED "? save failed (rc=%d)\n" SH_RST, (int)rc);
        return -1;
    }
    SHELL_PRINTF(SH_GREEN "saved %u bytes" SH_RST "\n", (unsigned)pos);
    return 0;
}

/**
 * @brief Read the FRAM-backed program text and replay it through
 *        process_line() to repopulate the in-memory line table.
 *
 * @return 0 on success, -1 if no saved program exists or persist
 *         read fails.
 */
static int
basic_load_from_persist(void)
{
    static char           tmp[TIKU_BASIC_SAVE_BUF_BYTES + 1];
    tiku_mem_arch_size_t  n_read = 0;
    tiku_mem_err_t        rc;
    char                 *line_start;
    char                 *p;

    if (basic_persist_ensure() != 0) {
        SHELL_PRINTF(SH_RED "? load: persist init failed\n" SH_RST);
        return -1;
    }
    rc = tiku_persist_read(&basic_store, BASIC_PERSIST_KEY,
            (uint8_t *)tmp, sizeof(tmp) - 1u, &n_read);
    if (rc == TIKU_MEM_ERR_NOT_FOUND || n_read == 0u) {
        SHELL_PRINTF(SH_RED "? load: no saved program\n" SH_RST);
        return -1;
    }
    if (rc != TIKU_MEM_OK) {
        SHELL_PRINTF(SH_RED "? load failed (rc=%d)\n" SH_RST, (int)rc);
        return -1;
    }
    tmp[n_read] = '\0';

    /* Wipe the in-memory program before loading so the saved version
     * is what the user actually gets (not merged onto stale lines). */
    prog_clear();

    /* Walk the buffer one line at a time, dispatching through
     * process_line.  Each line is a numbered statement, so each
     * call just stores it. */
    line_start = tmp;
    for (p = tmp; *p != '\0'; p++) {
        if (*p == '\n' || *p == '\r') {
            *p = '\0';
            if (line_start != p) {
                process_line(line_start);
            }
            line_start = p + 1;
        }
    }
    if (line_start && *line_start) {
        process_line(line_start);
    }

    SHELL_PRINTF(SH_GREEN "loaded %u bytes" SH_RST "\n", (unsigned)n_read);
    return 0;
}
