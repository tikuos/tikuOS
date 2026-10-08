/*
 * Tiku Operating System v0.06
 * Simple. Ubiquitous. Intelligence, Everywhere.
 * http://tiku-os.org
 *
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 *
 * tiku_shell_alias.h - user-defined shell shortcuts (NVM-backed).
 *
 * Consulted after a built-in lookup fails; the alias body may chain commands
 * with ';'.  Built-ins always win, so a misconfigured alias cannot lock out
 * help or reboot.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#ifndef TIKU_SHELL_ALIAS_H_
#define TIKU_SHELL_ALIAS_H_

#include <stdint.h>

/*---------------------------------------------------------------------------*/
/* CAPACITY                                                                  */
/*---------------------------------------------------------------------------*/

#define TIKU_SHELL_ALIAS_MAX        8   /**< Max alias slots */
#define TIKU_SHELL_ALIAS_NAME_MAX  15   /**< Max name length (excl. '\0') */
#define TIKU_SHELL_ALIAS_BODY_MAX  63   /**< Max body length (excl. '\0') */

/*---------------------------------------------------------------------------*/
/* RETURN CODES                                                              */
/*---------------------------------------------------------------------------*/

#define TIKU_SHELL_ALIAS_OK            0  /**< success */
#define TIKU_SHELL_ALIAS_ERR_FULL     -1  /**< no free slot */
#define TIKU_SHELL_ALIAS_ERR_NOTFOUND -2  /**< no alias by that name */
#define TIKU_SHELL_ALIAS_ERR_TOOBIG   -3  /**< name or body over its limit */
#define TIKU_SHELL_ALIAS_ERR_INVALID  -4  /**< NULL argument or empty name */

/*---------------------------------------------------------------------------*/
/* INIT                                                                      */
/*---------------------------------------------------------------------------*/

/**
 * @brief Validate the table's gate; prime every slot empty when it fails.
 *
 * Idempotent: a gate that holds the magic keeps the table as it is.
 *
 * @note The shell calls it once at startup.
 */
void tiku_shell_alias_init(void);

/*---------------------------------------------------------------------------*/
/* MUTATORS                                                                  */
/*---------------------------------------------------------------------------*/

/**
 * @brief Define an alias, or replace the body of an existing one.
 *
 * A new name takes the first free slot.
 *
 * @return TIKU_SHELL_ALIAS_OK; TIKU_SHELL_ALIAS_ERR_INVALID for a NULL or
 *         empty name or a NULL body; TIKU_SHELL_ALIAS_ERR_TOOBIG for a name or
 *         body over its limit; TIKU_SHELL_ALIAS_ERR_FULL with no free slot.
 */
int tiku_shell_alias_set(const char *name, const char *body);

/**
 * @brief Remove an alias by name.
 * @return TIKU_SHELL_ALIAS_OK, TIKU_SHELL_ALIAS_ERR_NOTFOUND, or
 *         TIKU_SHELL_ALIAS_ERR_INVALID for a NULL name.
 */
int tiku_shell_alias_clear(const char *name);

/*---------------------------------------------------------------------------*/
/* QUERIES                                                                   */
/*---------------------------------------------------------------------------*/

/**
 * @brief Look up an alias body by name.
 * @return Pointer to the body in the durable table, or NULL if not
 *         defined. Caller must not modify the returned string.
 */
const char *tiku_shell_alias_lookup(const char *name);

/**
 * @brief Read the alias in slot @p idx, for listing the table.
 *
 * Sets *@p name and *@p body, either of which may be NULL, to the strings in
 * the durable table.
 *
 * @return 1 for a defined alias; 0 for an empty slot or an @p idx at or past
 *         TIKU_SHELL_ALIAS_MAX.
 */
int tiku_shell_alias_get(uint8_t idx, const char **name,
                         const char **body);

/** @brief Number of currently-defined aliases. */
uint8_t tiku_shell_alias_count(void);

#endif /* TIKU_SHELL_ALIAS_H_ */
