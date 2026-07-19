/*
 * Tiku Operating System v0.05
 * Simple. Ubiquitous. Intelligence, Everywhere.
 * http://tiku-os.org
 *
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 *
 * tiku_basic_ext.h - native builtin registry for Tiku BASIC (Tier 2 of
 * kintsugi/loadable.md).
 *
 * Lets kernel services and tikukits register new BASIC words at boot,
 * without editing the interpreter: statements dispatch after the built-in
 * keyword chain (before implicit-LET), numeric functions after the built-in
 * function chain.  Registered names are never crunched (they are not in the
 * A2 token table), so they work identically in stored programs and immediate
 * mode via match_kw's raw-text path.
 *
 * Rules:
 *   - Names are UPPERCASE identifiers (A-Z, 0-9, '_'; must start with a
 *     letter; no '$' -- string-returning extensions are a future step),
 *     at most TIKU_BASIC_EXT_NAME_MAX-1 chars.
 *   - Names colliding with a crunched keyword are rejected (builtins win).
 *   - Registration is boot-time and idempotent (re-registering a name
 *     updates its handler).  There is no unregister.
 *   - Registered names become reserved words: like builtins, they take
 *     precedence over variables of the same name.
 *   - TIKU_BASIC_EXT_MAX (config) sizes the table; 0 compiles the whole
 *     feature out.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#ifndef TIKU_BASIC_EXT_H_
#define TIKU_BASIC_EXT_H_

#include <stddef.h>
#include <stdint.h>

/** Longest registered name incl. NUL. */
#define TIKU_BASIC_EXT_NAME_MAX 12

/**
 * @brief Statement handler.
 *
 * The cursor sits just past the keyword (trailing whitespace consumed).
 * Parse arguments with the services below; raise errors via
 * tiku_basic_ext_error().  On return the interpreter treats remaining
 * unconsumed text like any statement tail (':' continues, junk errors).
 */
typedef void (*tiku_basic_ext_stmt_fn)(const char **p);

/**
 * @brief Numeric function handler.
 *
 * The interpreter parses `(a[, b])` per the registered arity and passes the
 * evaluated values; argc is the arity.  Return 0 with *out set, or nonzero
 * after raising an error via tiku_basic_ext_error().
 */
typedef int (*tiku_basic_ext_nfn)(const long *args, int argc, long *out);

/**
 * @brief Register a statement word.
 * @return 0 on success; -1 on invalid name / keyword collision / table full.
 */
int tiku_basic_register_stmt(const char *name, tiku_basic_ext_stmt_fn fn);

/**
 * @brief Register a numeric function word with fixed arity 0..2.
 * @return 0 on success; -1 on invalid name / arity / collision / table full.
 */
int tiku_basic_register_fn(const char *name, uint8_t arity,
                           tiku_basic_ext_nfn fn);

/*---------------------------------------------------------------------------*/
/* Parser / error services for statement handlers.                           */
/* This is the minimal stable surface extensions may touch (and the ABI a    */
/* future native-module loader would program against -- see loadable.md).    */
/*---------------------------------------------------------------------------*/

/** Evaluate a numeric expression at the cursor. 0 on success, -1 on error. */
int tiku_basic_ext_parse_expr(const char **p, long *out);

/** Evaluate a string expression into buf. 0 on success, -1 on error (also
 *  -1 when the build has string support disabled). */
int tiku_basic_ext_parse_strexpr(const char **p, char *buf, size_t cap);

/** Raise an interpreter error (cat = TIKU_BASIC_ERR_*, msg = bare text).
 *  Routes through the A5 sink, so it works headless. */
void tiku_basic_ext_error(int cat, const char *msg);

/** Write @p s to the BASIC console (no newline added).  The output surface a
 *  statement extension needs -- same stream PRINT uses. */
void tiku_basic_ext_print(const char *s);

#endif /* TIKU_BASIC_EXT_H_ */
