/*
 * TikuOS kits -- the foundation TikuDesktop and TikuTracker
 * both stand on.
 *
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 *
 * tiku_textview.h - many lines of editable text, with a caret in them.
 *
 * tiku_field is one line: a name being typed over.  This is the other
 * kind -- a document, where Return makes a line rather than committing,
 * where the caret is a line and a column instead of an offset, and where
 * what is on screen is a window onto something taller.
 *
 * It owns the text, the caret and the scroll, and nothing else: the
 * pixels are the caller's, because a text view's look is the one thing
 * every application wants differently.  What it will not do is let the
 * caret point outside the text -- every edit below leaves the caret on a
 * real line at a real column, which is the invariant hand-rolled editors
 * get wrong first.
 *
 * SPDX-License-Identifier: Apache-2.0
 */
#ifndef TIKU_TEXTVIEW_H_
#define TIKU_TEXTVIEW_H_

#include <stddef.h>

#define TIKU_TEXTVIEW_LINES_MAX 4000
#define TIKU_TEXTVIEW_LINE_MAX  1024

typedef struct {
    char *text;                 /* NUL-terminated, owned */
    int   len;
} tiku_textview_line_t;

typedef struct {
    tiku_textview_line_t line[TIKU_TEXTVIEW_LINES_MAX];
    int nline;
    int cy, cx;                 /* the caret: line, then column */
    int top;                    /* first line shown             */
    int modified;
} tiku_textview_t;

/** @brief Start on one empty line, which is what an empty document is. */
void tiku_textview_init(tiku_textview_t *tv);

/** @brief Give back every line.  The view is empty after, not invalid. */
void tiku_textview_free(tiku_textview_t *tv);

/**
 * @brief Replace the whole text, splitting @p text on newlines.
 *
 * A trailing newline does NOT make a final empty line: a file that ends
 * as files do would otherwise grow a blank line every time it was opened
 * and saved.
 */
void tiku_textview_set(tiku_textview_t *tv, const char *text);

/** @brief How many lines there are (never fewer than one). */
int tiku_textview_lines(const tiku_textview_t *tv);

/** @brief Line @p i, or "" for one that does not exist. */
const char *tiku_textview_line(const tiku_textview_t *tv, int i);

/** @brief Length of line @p i. */
int tiku_textview_line_len(const tiku_textview_t *tv, int i);

/** @brief Whether it has been edited since the last set or save. */
int tiku_textview_modified(const tiku_textview_t *tv);

/** @brief Say it has been saved: the modified flag clears. */
void tiku_textview_saved(tiku_textview_t *tv);

/** @brief Where the caret is. */
void tiku_textview_caret(const tiku_textview_t *tv, int *line, int *col);

/** @brief The first line on screen. */
int tiku_textview_top(const tiku_textview_t *tv);

/** @brief Put the caret on @p line, @p col, clamped into the text. */
void tiku_textview_place(tiku_textview_t *tv, int line, int col);

/** @brief Type one character at the caret. */
void tiku_textview_insert(tiku_textview_t *tv, char ch);

/** @brief Break the line at the caret; the tail becomes the next line. */
void tiku_textview_newline(tiku_textview_t *tv);

/**
 * @brief Rub out backwards.
 *
 * At the head of a line this joins it to the one above, which is where
 * the caret lands -- at the seam, not at the start.
 */
void tiku_textview_backspace(tiku_textview_t *tv);

/** @brief Rub out forwards, joining the next line at a line's end. */
void tiku_textview_delete(tiku_textview_t *tv);

/** @brief Which way the caret is being moved. */
typedef enum {
    TIKU_TEXTVIEW_LEFT = 0,
    TIKU_TEXTVIEW_RIGHT,
    TIKU_TEXTVIEW_UP,
    TIKU_TEXTVIEW_DOWN,
    TIKU_TEXTVIEW_HOME,
    TIKU_TEXTVIEW_END
} tiku_textview_move_t;

/**
 * @brief Move the caret.
 *
 * Left at the head of a line goes to the END of the one above, and right
 * at the end goes to the head of the one below: the caret walks the text
 * as one run, the way it reads, rather than stopping at line ends.
 */
void tiku_textview_move(tiku_textview_t *tv, tiku_textview_move_t how);

/**
 * @brief Scroll so the caret is among the @p rows lines on screen.
 *
 * @return nonzero when the first line shown changed.
 */
int tiku_textview_reveal(tiku_textview_t *tv, int rows);

/** @brief Scroll to @p top directly, clamped to the text. */
int tiku_textview_scroll_to(tiku_textview_t *tv, int top, int rows);

/**
 * @brief Write the whole text into @p out, lines joined by newlines.
 *
 * @return the length written, or the length it would need when @p out is
 *         too small -- so a caller can size a buffer by asking twice.
 */
size_t tiku_textview_text(const tiku_textview_t *tv, char *out, size_t max);

#endif /* TIKU_TEXTVIEW_H_ */
