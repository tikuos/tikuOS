/*
 * Tiku Operating System v0.04
 * Simple. Ubiquitous. Intelligence, Everywhere.
 * http://tiku-os.org
 *
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 *
 * tiku_shell_alias.h - User-defined shell shortcuts (FRAM-backed)
 *
 * The shell consults this table after a built-in command lookup
 * fails. If the typed token matches an alias, the parser dispatches
 * the alias body (which may contain ';' separators for chained
 * commands). Built-in commands always win over aliases so a
 * misconfigured alias cannot lock out 'help' or 'reboot'.
 *
 * Storage lives in the .persistent linker section, so aliases
 * survive reset, brownout, and power loss. A magic-word gate
 * disambiguates virgin FRAM from a real persisted table.
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at:
 *
 *     http://www.apache.org/licenses/LICENSE-2.0
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

#define TIKU_SHELL_ALIAS_OK            0
#define TIKU_SHELL_ALIAS_ERR_FULL     -1
#define TIKU_SHELL_ALIAS_ERR_NOTFOUND -2
#define TIKU_SHELL_ALIAS_ERR_TOOBIG   -3
#define TIKU_SHELL_ALIAS_ERR_INVALID  -4

/*---------------------------------------------------------------------------*/
/* INIT                                                                      */
/*---------------------------------------------------------------------------*/

/**
 * @brief Validate the FRAM magic word; first-boot the table if absent.
 *
 * Idempotent. Safe to call multiple times. Should be called once
 * during shell startup.
 */
void tiku_shell_alias_init(void);

/*---------------------------------------------------------------------------*/
/* MUTATORS                                                                  */
/*---------------------------------------------------------------------------*/

/**
 * @brief Define or overwrite an alias.
 * @return TIKU_SHELL_ALIAS_OK or a negative error code.
 *
 * If @p name already exists, its body is replaced. Otherwise the
 * first empty slot is used. Returns TIKU_SHELL_ALIAS_ERR_FULL if
 * neither path is available.
 */
int tiku_shell_alias_set(const char *name, const char *body);

/**
 * @brief Remove an alias by name.
 * @return TIKU_SHELL_ALIAS_OK or TIKU_SHELL_ALIAS_ERR_NOTFOUND.
 */
int tiku_shell_alias_clear(const char *name);

/*---------------------------------------------------------------------------*/
/* QUERIES                                                                   */
/*---------------------------------------------------------------------------*/

/**
 * @brief Look up an alias body by name.
 * @return Pointer to FRAM-resident body string, or NULL if not
 *         defined. Caller must not modify the returned string.
 */
const char *tiku_shell_alias_lookup(const char *name);

/**
 * @brief Iterate the alias table.
 *
 * Pass @p idx in the range [0, TIKU_SHELL_ALIAS_MAX). On success
 * sets *@p name and *@p body to FRAM-resident strings and returns
 * 1. Returns 0 if the slot at @p idx is empty.
 */
int tiku_shell_alias_get(uint8_t idx, const char **name,
                         const char **body);

/** @brief Number of currently-defined aliases. */
uint8_t tiku_shell_alias_count(void);

#endif /* TIKU_SHELL_ALIAS_H_ */
