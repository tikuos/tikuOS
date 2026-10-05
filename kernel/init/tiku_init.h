/*
 * Tiku Operating System v0.06
 * Simple. Ubiquitous. Intelligence, Everywhere.
 * http://tiku-os.org
 *
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 *
 * tiku_init.h - NVM-backed configurable boot (init system).
 *
 * Stores an ordered table of shell commands in NVM; tiku_init_run_all() feeds
 * each enabled entry through the shell parser at boot.  The table lives in a
 * config region from the NVM region map.
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

/** Size of an entry's name field, NUL included */
#define TIKU_INIT_NAME_SIZE     16

/** Size of an entry's command field, NUL included */
#define TIKU_INIT_CMD_SIZE      48

/*---------------------------------------------------------------------------*/
/* TYPES                                                                     */
/*---------------------------------------------------------------------------*/

/**
 * @brief Single init-table entry stored in NVM.
 *
 * A stored table is read back with this layout by later firmware, so a change
 * to it also changes TIKU_INIT_MAGIC (tiku_init.c), which primes the table
 * empty.
 */
typedef struct {
    uint8_t  seq;                          /**< Boot order, lower first; the
                                                init command takes 0-99 */
    uint8_t  enabled;                      /**< 1 = active, 0 = skipped */
    char     name[TIKU_INIT_NAME_SIZE];    /**< Human-readable label */
    char     cmd[TIKU_INIT_CMD_SIZE];      /**< Shell command to execute */
} tiku_init_entry_t;

/*---------------------------------------------------------------------------*/
/* PUBLIC API                                                                */
/*---------------------------------------------------------------------------*/

/**
 * @brief Load and validate the init table from NVM.
 *
 * A blank table, or one that fails the checks, is primed empty; a missing or
 * undersized region disables the table.
 *
 * @note Call once during boot, after the NVM region map is initialised.  A
 *       power loss during an add or an enable leaves the old table or the new
 *       one; during a replace or a remove it can leave the table empty, never
 *       torn.
 */
void tiku_init_load(void);

/**
 * @brief Execute all enabled entries in sequence-number order.
 *
 * Each entry's cmd is copied to a stack buffer and passed to
 * tiku_shell_parser_execute().
 *
 * @note The shell process calls it in its first pass, once the parser has its
 *       command table.
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
 * @return 0 on success, -1 if the table is full or unusable or the write fails
 */
int8_t tiku_init_add(uint8_t seq, const char *name, const char *cmd);

/**
 * @brief Remove an entry by name.
 *
 * @param name  Entry name to remove
 * @return 0 on success, -1 if not found, the table is unusable or the write
 *         fails
 */
int8_t tiku_init_remove(const char *name);

/**
 * @brief Enable or disable an entry by name.
 *
 * @param name  Entry name
 * @param en    1 = enable, 0 = disable
 * @return 0 on success, -1 if not found, the table is unusable or the write
 *         fails
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
