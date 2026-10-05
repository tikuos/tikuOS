/*
 * Tiku Operating System v0.06
 * Simple. Ubiquitous. Intelligence, Everywhere.
 * http://tiku-os.org
 *
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 *
 * tiku_basic_cursor.inl - the parse-cursor vocabulary.
 *
 * Inline helpers for the one cursor that the lexer, the expression parser and
 * every statement handler share: peek, consume, match and probe.
 *
 * SPDX-License-Identifier: Apache-2.0
 */
/*
 * Invariants of the vocabulary:
 *
 *  - The buffer behind the cursor is NUL-terminated and every parser loop
 *    stops at '\0'.  The helpers take no length, so a lookahead must not
 *    pass the NUL.
 *  - Crunched program lines hold keyword bytes >= BASIC_TOK_BASE in the
 *    same buffer.  Read those through cur_peekb(), which returns uint8_t:
 *    cur_peek() returns char, whose sign for bytes >= 0x80 is
 *    implementation-defined.
 *  - No helper skips whitespace: cur_match() consumes one character, and a
 *    grammar rule that allows blanks calls skip_ws().  Some rules are
 *    whitespace-sensitive: the `$` sigil touches its identifier, and a
 *    string literal takes every byte.
 *
 * Two cursor modes share one representation:
 *
 *  - Committed: a helper taking `const char **p` advances the cursor that
 *    every caller up the chain shares.
 *  - Probe: cur_mark() copies the position, the probe scans or parses
 *    ahead, then either keeps the new position or calls cur_rewind() to
 *    return to the mark.  A probe that cannot fail needs no mark.
 */

/*---------------------------------------------------------------------------*/
/* PEEK -- look, consume nothing                                             */
/*---------------------------------------------------------------------------*/

/** @brief Current character; does not consume. */
static inline char
cur_peek(const char **p)
{
    return **p;
}

/** @brief Character @p n positions ahead; does not consume.
 *  @note Every byte before offset @p n must be known non-NUL. */
static inline char
cur_peek_at(const char **p, int n)
{
    return *(*p + n);
}

/** @brief Current byte as uint8_t -- for crunched-token tests
 *  (bytes >= BASIC_TOK_BASE); does not consume. */
static inline uint8_t
cur_peekb(const char **p)
{
    return (uint8_t)**p;
}

/*---------------------------------------------------------------------------*/
/* CONSUME -- step the cursor forward                                        */
/*---------------------------------------------------------------------------*/

/** @brief Consume one character. */
static inline void
cur_advance(const char **p)
{
    (*p)++;
}

/** @brief Consume one character and return it. */
static inline char
cur_take(const char **p)
{
    char c = **p;
    (*p)++;
    return c;
}

/** @brief Consume @p n characters (known-length token, e.g. a
 *  two-character prefix already verified by lookahead). */
static inline void
cur_skip(const char **p, int n)
{
    (*p) += n;
}

/** @brief If the current character is @p c, consume it and return 1;
 *  otherwise consume nothing and return 0.  Skips no whitespace. */
static inline int
cur_match(const char **p, char c)
{
    if (**p != c) return 0;
    (*p)++;
    return 1;
}

/*---------------------------------------------------------------------------*/
/* PROBE -- try ahead without committing                                     */
/*---------------------------------------------------------------------------*/

/** @brief Save the cursor position before a probe. */
static inline const char *
cur_mark(const char **p)
{
    return *p;
}

/** @brief Un-consume everything since @p m (probe failed). */
static inline void
cur_rewind(const char **p, const char *m)
{
    *p = m;
}

/** @brief Move the cursor to @p pos, a position a probe or scan derived;
 *  cur_rewind() is the same store, named for a backtrack. */
static inline void
cur_set(const char **p, const char *pos)
{
    *p = pos;
}
