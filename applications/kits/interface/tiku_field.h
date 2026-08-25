/*
 * TikuOS kits -- the foundation TikuDesktop and TikuTracker
 * both stand on.
 *
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 *
 * tiku_field.h - the editing state behind a single-line text field.
 *
 * The state behind tiku_ui_textfield(), which paints one.  Grown in
 * Tracker as the rename editor, and moved here because every
 * application's field wants the same caret, selection, deny-set and
 * clipboard rules -- three programs re-deriving them is how they end
 * up agreeing on none.
 *
 * Tracker's BTextWidget hands editing to a BTextView placed over the row.
 * The behaviours that matter are not the text view's: the editor opens with
 * everything selected so the first keystroke replaces the old name, Escape
 * abandons and Return commits, and while it is up the view's own keys are
 * off -- an arrow moves the caret, not the selection.
 *
 * SPDX-License-Identifier: Apache-2.0
 */
#ifndef TIKU_FIELD_H_
#define TIKU_FIELD_H_

#include <stddef.h>

/**
 * @brief The editor's ceiling.
 *
 * Not smaller than the longest value a pose can hold: an editor that opens
 * already truncated commits the truncation, silently shortening a value the
 * user never touched.
 */
#define TIKU_FIELD_MAX 256

/** @brief Keys the editor understands beyond ordinary characters. */
typedef enum {
    TIKU_FIELD_CHAR = 0,
    TIKU_FIELD_LEFT,
    TIKU_FIELD_RIGHT,
    TIKU_FIELD_HOME,
    TIKU_FIELD_END,
    TIKU_FIELD_BACKSPACE,
    TIKU_FIELD_DELETE,
    TIKU_FIELD_SELECT_ALL,
    TIKU_FIELD_CUT,
    TIKU_FIELD_COPY,
    TIKU_FIELD_PASTE,
    TIKU_FIELD_COMMIT,
    TIKU_FIELD_CANCEL
} tiku_field_key_t;

/** @brief What the caller should do after a key. */
typedef enum {
    TIKU_FIELD_IGNORED = 0,
    TIKU_FIELD_HANDLED,
    TIKU_FIELD_COMMITTED,
    TIKU_FIELD_CANCELLED
} tiku_field_result_t;

/** @brief An open editor: text, caret and the selected range. */
typedef struct {
    char text[TIKU_FIELD_MAX];
    int  len;
    int  caret;
    int  sel_a, sel_b;              /* equal means nothing is selected     */
    int  active;
    int  editable;                  /* selectable-only fields keep focus   */
    /* Characters this field will not accept at all.  A name's illegal set
     * belongs to the STORE -- a namespace path and a file name forbid
     * different things -- so it is supplied rather than hardcoded. */
    const char *deny;
} tiku_field_t;

/**
 * @brief Open the editor over @p text, with all of it selected.
 *
 * Selected because the common rename is a REPLACEMENT: typing over an
 * inherited name is the case, and having to clear it first would make every
 * rename two operations.
 */
void tiku_field_start(tiku_field_t *e, const char *text);

/**
 * @brief Refuse @p deny's characters at the keystroke (MA-046).
 *
 * Set after starting.  NULL accepts anything printable.
 */
void tiku_field_deny(tiku_field_t *e, const char *deny);

/** @brief Keep the field selectable while refusing text mutations. */
void tiku_field_set_editable(tiku_field_t *e, int editable);

/** @brief Feed one key.  @p ch is used only for TIKU_FIELD_CHAR. */
tiku_field_result_t tiku_field_key(tiku_field_t *e,
                                         tiku_field_key_t key,
                                         int shift, char ch);

/** @brief Close without deciding; the text is left as it stands. */
void tiku_field_stop(tiku_field_t *e);

/**
 * @brief How wide the editor should draw, and how far to slide (MA-049).
 *
 * Grows with the text between @p min_w and @p max_w; past the cap the box
 * stays at @p max_w and @p scroll_px slides the text so the caret is
 * visible.  @p measure_prefix measures the first n bytes of the text.
 */
void tiku_field_metrics(const tiku_field_t *e, int min_w, int max_w,
                           int (*measure_prefix)(const char *, int, void *),
                           void *ctx, int *out_w, int *out_scroll);

/** @brief The selected range, ordered.  @return 1 when there is one. */
int tiku_field_selection(const tiku_field_t *e, int *from, int *to);

#endif /* TIKU_FIELD_H_ */
