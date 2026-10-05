/*
 * Tiku Operating System v0.06
 * Simple. Ubiquitous. Intelligence, Everywhere.
 * http://tiku-os.org
 *
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 *
 * tiku_basic_ext.h - native builtin registry for Tiku BASIC.
 *
 * Lets kernel services register new words at boot without editing the
 * interpreter: statements dispatch after the built-in keyword chain and
 * functions after the built-in function chain, so builtins win.
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
 * The cursor sits just past the keyword, trailing whitespace consumed.  Parse
 * arguments with the services below and raise errors via
 * tiku_basic_ext_error().
 *
 * @note On return the interpreter treats remaining unconsumed text like any
 *       statement tail: ':' continues, junk errors.
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
 * @brief String-returning function handler (`NAME$`).
 *
 * Unlike numeric functions the handler parses its own arguments, so it can take
 * string args, numeric args or a mix; on entry the cursor sits just past the
 * name.  Write the result into @p out (capacity @p cap, always NUL-terminated).
 *
 * @note Use tiku_basic_ext_expect() for '(' / ',' / ')' and
 *       tiku_basic_ext_parse_expr / _parse_strexpr for the arguments; raise
 *       errors via tiku_basic_ext_error().
 */
typedef void (*tiku_basic_ext_strfn)(const char **p, char *out, size_t cap);

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

/**
 * @brief Register a string-returning function word.
 *
 * The handler parses its own arguments (see tiku_basic_ext_strfn).
 *
 * @note @p name ends in '$'.
 * @return 0 on success; -1 on invalid name / collision / table full, or when
 *         the build has string support disabled.
 */
int tiku_basic_register_strfn(const char *name, tiku_basic_ext_strfn fn);

/*---------------------------------------------------------------------------*/
/* PARSER / ERROR SERVICES                                                   */
/*---------------------------------------------------------------------------*/

/* The surface extension handlers may use.  The native-module loader hands the
 * same services to a module as tiku_basic_syscalls_t (tiku_basic_module.h). */

/**
 * @brief Evaluate a numeric expression at the cursor into @p out.
 * @return 0 on success, -1 on error.
 */
int tiku_basic_ext_parse_expr(const char **p, long *out);

/**
 * @brief Evaluate a string expression at the cursor into @p buf.
 * @return 0 on success; -1 on error, or when the build has string support
 *         disabled.
 */
int tiku_basic_ext_parse_strexpr(const char **p, char *buf, size_t cap);

/**
 * @brief Raise an interpreter error.
 *
 * Routes through the error sink (tiku_basic_set_error_sink()), so it works
 * headless.
 *
 * @param cat  Category, TIKU_BASIC_ERR_*
 * @param msg  Bare message text
 */
void tiku_basic_ext_error(int cat, const char *msg);

/**
 * @brief Write @p s to the BASIC console, the stream PRINT uses.
 *
 * No newline is added.
 */
void tiku_basic_ext_print(const char *s);

/**
 * @brief Skip whitespace, then require and consume @p ch (e.g. '(' ',' ')').
 * @return 0 on success; -1 after raising a syntax error.
 */
int tiku_basic_ext_expect(const char **p, char ch);

#endif /* TIKU_BASIC_EXT_H_ */
