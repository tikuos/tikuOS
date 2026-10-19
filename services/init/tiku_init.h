/*
 * Tiku Operating System v0.06
 * Simple. Persistent. Intelligent.
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
 * A blank table, or one that fails the checks, is primed empty.  A missing or
 * undersized region, or a prime that fails, disables the table: every other
 * function then returns its failure value.
 *
 * @note Call once during boot, after the NVM region map is initialised and
 *       before tiku_init_run_all().  A power loss during an add or an enable
 *       leaves the old table or the new one; during a replace or a remove it
 *       can leave the table empty, never torn.
 */
void tiku_init_load(void);

/**
 * @brief Execute every enabled, non-empty entry in sequence-number order.
 *
 * Each entry's cmd is copied to a stack buffer, printed on an "[init]" line
 * and passed to tiku_shell_parser_execute().  Does not write the table
 * itself; a command it runs can.
 *
 * @note The entries present at the call run at most once each, in the order
 *       they had then: one a command removes or disables before its turn is
 *       skipped, one it adds waits for the next run, and a run a command
 *       starts returns 0.
 * @note The shell process calls it in its first pass, once the parser has its
 *       command table.
 * @return Number of entries executed; 0 if the table is unusable or empty,
 *         or a run is in progress
 */
uint8_t tiku_init_run_all(void);

/**
 * @brief Add or replace an init entry; the stored entry is enabled.
 *
 * An entry with the same name is replaced, a disabled one included; otherwise
 * the entry takes the next free slot.
 *
 * @param seq   Boot sequence number (lower = earlier)
 * @param name  Entry name, truncated to TIKU_INIT_NAME_SIZE-1 chars
 * @param cmd   Shell command string, truncated to TIKU_INIT_CMD_SIZE-1 chars
 * @return 0 on success (added or replaced); -1 if the table is unusable, an
 *         argument is NULL, the table is full on add, or the write fails
 */
int8_t tiku_init_add(uint8_t seq, const char *name, const char *cmd);

/**
 * @brief Remove an entry by name.
 *
 * The last entry moves into the freed slot and takes its tiku_init_get()
 * index; the run order changes only among entries that share a seq.
 *
 * @param name  Entry name to remove
 * @return 0 on success; -1 if @p name is NULL or not found, the table is
 *         unusable or the write fails
 */
int8_t tiku_init_remove(const char *name);

/**
 * @brief Enable or disable an entry by name.
 *
 * Writes only the enabled flag.  A disabled entry stays in the table and in
 * tiku_init_count() but tiku_init_run_all() skips it.
 *
 * @param name  Entry name
 * @param en    Non-zero = enable, 0 = disable
 * @return 0 on success; -1 if @p name is NULL or not found, the table is
 *         unusable or the write fails
 */
int8_t tiku_init_enable(const char *name, uint8_t en);

/**
 * @brief Return the number of entries in the table (active + disabled).
 *
 * @return Entry count, or 0 if the table is unusable
 */
uint8_t tiku_init_count(void);

/**
 * @brief Get a read-only pointer to an entry by index.
 *
 * The pointer is into the NVM table, not a copy: a later add, remove or
 * enable changes what it points to.
 *
 * @param idx  Index (0 .. tiku_init_count()-1)
 * @return Pointer to entry, or NULL if the table is unusable or idx is out of
 *         range
 * @note Do not write through it; change the table with tiku_init_add(),
 *       tiku_init_remove() or tiku_init_enable().
 */
const tiku_init_entry_t *tiku_init_get(uint8_t idx);

#endif /* TIKU_INIT_H_ */
