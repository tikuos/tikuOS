/*
 * TikuOS kits -- the foundation TikuDesktop and TikuTracker
 * both stand on.
 *
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 *
 * tiku_syntax.h - one line of a program, told apart into what its parts
 * MEAN.
 *
 * Given a line, this says which stretches of it are words the language
 * reserves, numbers, quoted text, and remarks the machine ignores.  It
 * paints nothing and knows nothing about colour: it hands back a table
 * of tiku_span_t, and a painter asks the theme what each meaning looks
 * like.  Which is why a highlighted window re-themes with everything
 * else instead of carrying a second palette of its own.
 *
 * The one language it knows is TikuOS BASIC, and it knows it from the
 * interpreter's own tables rather than from a plausible-looking list --
 * kernel/shell/basic/tiku_basic_token.inl's crunch table plus every
 * spelling the dispatchers match by hand.  Highlighting that lies is
 * worse than none: a word painted as reserved that the interpreter would
 * treat as a variable teaches the reader something false about their own
 * program, and they will believe it, because the machine said it.
 *
 * CLASSIFICATION IS LINE-LOCAL, and that is a fact about BASIC rather
 * than a simplification: a remark runs to the end of its line, a quoted
 * string is closed by the line's end if the quote never comes, and there
 * is no continuation and no block comment.  Nothing carries across, so
 * a line can be told apart on its own -- which is what lets a window
 * classify only the lines it is showing, and never re-scan a document to
 * paint one row.  A language with block comments could not use this
 * signature, and should not pretend to.
 *
 * SPDX-License-Identifier: Apache-2.0
 */
#ifndef TIKU_SYNTAX_H_
#define TIKU_SYNTAX_H_

#include "tiku_gfx.h"

/** @brief The languages this build can tell apart. */
typedef enum {
    TIKU_SYNTAX_NONE = 0,   /* prose: every byte is the document's ink */
    TIKU_SYNTAX_BASIC       /* TikuOS BASIC                            */
} tiku_syntax_lang_t;

/**
 * @brief Tell @p line apart into runs of meaning, into @p out.
 *
 * Runs are returned in order and cover the line from its first byte; a
 * caller paints them in order and gives whatever they do not reach the
 * plain ink, so a table that stops short is a shorter answer and never a
 * wrong one.  Adjacent plain stretches are merged, which is what keeps
 * an ordinary line to one run.
 *
 * @param max How many runs @p out can hold.  Past it the line simply
 *            stops being told apart -- the remainder is plain, which is
 *            legible, rather than truncated, which would not be.
 * @return the number of runs written; 0 for a NULL or empty line, and 0
 *         for TIKU_SYNTAX_NONE, which is prose by definition.
 */
int tiku_syntax_spans(tiku_syntax_lang_t lang, const char *line,
                      tiku_span_t *out, int max);

/**
 * @brief Whether @p name is a word TikuOS BASIC reserves.
 *
 * Case-insensitive, and the whole word must match -- PRINTER is not
 * PRINT.  Exposed because "is this word taken" is a question an editor
 * asks for its own reasons (a name it is about to accept, a completion
 * it is about to offer), not only while painting.
 */
int tiku_syntax_basic_word(const char *name);

/**
 * @brief The language a file called @p path is written in.
 *
 * By its suffix, because that is the only thing a name can honestly
 * say: ".bas" is BASIC and everything else is prose.  Sniffing the
 * contents would guess, and a guess that highlights an ordinary letter
 * as a program is the lie this whole file exists to avoid.
 */
tiku_syntax_lang_t tiku_syntax_of_path(const char *path);

#endif /* TIKU_SYNTAX_H_ */
