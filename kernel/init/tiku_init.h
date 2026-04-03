/*
 * Tiku Operating System
 * http://tiku-os.org
 *
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 *
 * tiku_init.h - FRAM-backed configurable boot (init system)
 *
 * Stores an ordered table of shell commands in FRAM.  At boot,
 * tiku_init_run_all() feeds each enabled entry through the shell
 * parser — making the boot sequence configurable without recompiling.
 *
 * The init table lives inside the FRAM config region managed by
 * kernel/memory/tiku_fram_map.
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at:
 *
 *     http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#ifndef TIKU_INIT_H_
#define TIKU_INIT_H_

/*---------------------------------------------------------------------------*/
/* INCLUDES                                                                  */
/*---------------------------------------------------------------------------*/

#include <stdint.h>

/*---------------------------------------------------------------------------*/
/* CONFIGURATION                                                             */
/*---------------------------------------------------------------------------*/

/** Maximum number of init entries */
#ifndef TIKU_INIT_MAX_ENTRIES
#define TIKU_INIT_MAX_ENTRIES   8
#endif

/** Maximum length of an entry name (including NUL) */
#define TIKU_INIT_NAME_SIZE     16

/** Maximum length of the shell command (including NUL) */
#define TIKU_INIT_CMD_SIZE      48

/*---------------------------------------------------------------------------*/
/* TYPES                                                                     */
/*---------------------------------------------------------------------------*/

/**
 * @brief Single init-table entry stored in FRAM.
 *
 * Layout is fixed so the FRAM image is portable across firmware versions
 * (as long as sizes remain the same).
 */
typedef struct {
    uint8_t  seq;                          /**< Boot order (0–99) */
    uint8_t  enabled;                      /**< 1 = active, 0 = skipped */
    char     name[TIKU_INIT_NAME_SIZE];    /**< Human-readable label */
    char     cmd[TIKU_INIT_CMD_SIZE];      /**< Shell command to execute */
} tiku_init_entry_t;

/*---------------------------------------------------------------------------*/
/* PUBLIC API                                                                */
/*---------------------------------------------------------------------------*/

/**
 * @brief Load and validate the init table from FRAM.
 *
 * Call once during boot, after tiku_fram_map_init().
 * Auto-initialises the table on first boot (blank FRAM).
 */
void tiku_init_load(void);

/**
 * @brief Execute all enabled entries in sequence-number order.
 *
 * Each entry's cmd is copied to an SRAM scratch buffer and passed
 * to tiku_shell_parser_execute().
 *
 * @return Number of entries executed
 */
uint8_t tiku_init_run_all(void);

/**
 * @brief Add or replace an init entry.
 *
 * If an entry with the same name exists, it is replaced.
 * Otherwise a free slot is used.
 *
 * @param seq   Boot sequence number (lower = earlier)
 * @param name  Entry name (max TIKU_INIT_NAME_SIZE-1 chars)
 * @param cmd   Shell command string (max TIKU_INIT_CMD_SIZE-1 chars)
 * @return 0 on success, -1 if table is full
 */
int8_t tiku_init_add(uint8_t seq, const char *name, const char *cmd);

/**
 * @brief Remove an entry by name.
 *
 * @param name  Entry name to remove
 * @return 0 on success, -1 if not found
 */
int8_t tiku_init_remove(const char *name);

/**
 * @brief Enable or disable an entry by name.
 *
 * @param name  Entry name
 * @param en    1 = enable, 0 = disable
 * @return 0 on success, -1 if not found
 */
int8_t tiku_init_enable(const char *name, uint8_t en);

/**
 * @brief Return the number of entries in the table (active + disabled).
 */
uint8_t tiku_init_count(void);

/**
 * @brief Get a read-only pointer to an entry by index.
 *
 * @param idx  Index (0 .. tiku_init_count()-1)
 * @return Pointer to entry, or NULL if idx is out of range
 */
const tiku_init_entry_t *tiku_init_get(uint8_t idx);

#endif /* TIKU_INIT_H_ */
