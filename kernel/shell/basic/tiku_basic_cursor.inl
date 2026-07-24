/*
 * Tiku Operating System v0.06
 * Simple. Ubiquitous. Intelligence, Everywhere.
 * http://tiku-os.org
 *
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 *
 * tiku_basic_cursor.inl - the parse-cursor vocabulary.
 *
 * NOT a standalone translation unit.  Included from tiku_basic.c
 * (before every other parsing piece).
 *
 * The interpreter threads ONE cursor -- `const char **p` -- through
 * the lexer, the expression parser, and every statement handler.
 * These inline helpers name the handful of raw pointer operations
 * the whole parser is built from, so call sites read as intent
 * (peek / advance / match) rather than as pointer mechanics
 * (**p / (*p)++ / *(*p + 1)).
 *
 * Invariants the vocabulary relies on:
 *
 *  - The buffer behind the cursor is NUL-terminated.  Bounded
 *    lookahead (cur_peek_at) is therefore safe without a length:
 *    once a NUL is seen, no helper reads past it, and every parser
 *    loop terminates on '\0'.
 *  - Crunched program lines (A2) store keyword bytes >=
 *    BASIC_TOK_BASE in the same buffer.  Read those through
 *    cur_peekb(), which yields the byte as uint8_t -- plain
 *    cur_peek() returns char, whose sign for bytes >= 0x80 is
 *    implementation-defined.
 *  - Whitespace handling is NEVER implicit.  cur_match() consumes
 *    exactly one character and skips nothing; where a grammar rule
 *    tolerates blanks, the call site says so with skip_ws().
 *    (Several rules are deliberately ws-sensitive: the `$` sigil
 *    must touch its identifier, string literals take every byte.)
 *
 * Two cursor modes, one representation:
 *
 *  - COMMITTED: helpers taking `const char **p` advance the shared
 *    cursor; the caller (and its caller) see the consumption.
 *  - PROBE: cur_mark() copies the position, the probe scans or
 *    parses ahead, then either falls through (commit) or calls
 *    cur_rewind() to un-consume everything since the mark.  A probe
 *    that cannot fail needs no mark.
 *
 * SPDX-License-Identifier: Apache-2.0
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
 *  Safe for any @p n that cannot skip past the terminating NUL. */
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

/** @brief Consume one character.  Returns nothing by design: use
 *  cur_take() when the consumed character is wanted, so a call can
 *  never silently both test and consume. */
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

/** @brief Move the cursor to @p pos (probe succeeded / jump to an
 *  already-derived position).  Same mechanics as cur_rewind -- the
 *  two names keep commit-forward and backtrack distinguishable at
 *  the call site. */
static inline void
cur_set(const char **p, const char *pos)
{
    *p = pos;
}
