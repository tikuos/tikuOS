/*
 * TikuOS kits -- the foundation TikuDesktop and TikuTracker
 * both stand on.
 *
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 *
 * tiku_form.h - a panel that was DESCRIBED rather than programmed.
 *
 * A few lines of text say what controls there are, what each is called,
 * and -- the part that matters -- which PLACE in the namespace each one
 * shows or changes:
 *
 *     title   Board one
 *     gauge   Temperature  /devices/board1/sys/temp  0 100
 *     toggle  Lamp         /devices/board1/led
 *     field   Name         /devices/board1/name
 *     button  Reboot       /devices/board1/reboot    1
 *
 * The shell renders that, tracks it, reads the bound places to fill it
 * in, and writes to them when somebody works a control.  So a device
 * with NO interface code of its own has an interface: it publishes what
 * it has, in text, and the desktop is the program that draws it.
 *
 * This is the whole thesis in one file.  The desktop IS the device's
 * interface; the device does not ship a second one, does not link a
 * toolkit, and does not care what the desktop looks like this year.
 * And because the description names PATHS, an interface for a board
 * reached over a serial line is written the same way as one for a
 * folder of files -- the namespace already made those the same kind of
 * thing.
 *
 * WHAT IT DELIBERATELY IS NOT: a layout language.  There are no
 * coordinates, no sizes and no nesting, because a description that
 * carried them would be a description that has to be re-authored for
 * every screen it lands on -- and the reason to describe a panel rather
 * than draw one is that the drawing end knows things the describing end
 * cannot: the face, the theme, how much room there is.  Rows in the
 * order they were written is the whole of the arrangement.
 *
 * SPDX-License-Identifier: Apache-2.0
 */
#ifndef TIKU_FORM_H_
#define TIKU_FORM_H_

#include "tiku_gfx.h"

#define TIKU_FORM_ROWS   16
#define TIKU_FORM_LABEL  40
#define TIKU_FORM_BIND   256
#define TIKU_FORM_VALUE  64
#define TIKU_FORM_TITLE  48

/** @brief What a row IS. */
typedef enum {
    TIKU_FORM_TEXT = 0,     /* words, and nothing to work            */
    TIKU_FORM_GAUGE,        /* how full something is                 */
    TIKU_FORM_TOGGLE,       /* on or off, and it can be changed      */
    TIKU_FORM_FIELD,        /* what a place says, shown as words     */
    TIKU_FORM_BUTTON        /* a press writes a value to the place   */
} tiku_form_kind_t;

typedef struct {
    unsigned char kind;
    char label[TIKU_FORM_LABEL];
    char bind[TIKU_FORM_BIND];      /* the place it shows or changes */
    char value[TIKU_FORM_VALUE];    /* what that place last said     */
    int  lo, hi;                    /* a gauge's ends                */
    int  known;                     /* nonzero once value was read   */
} tiku_form_row_t;

typedef struct {
    char title[TIKU_FORM_TITLE];
    int  nrow;
    tiku_form_row_t row[TIKU_FORM_ROWS];
} tiku_form_t;

/**
 * @brief Read a description into @p f.
 *
 * A line the parser does not understand is SKIPPED rather than
 * refusing the whole description: a panel from a device built after
 * this desktop will have rows this desktop has never heard of, and
 * showing the rows it does understand is better than showing nothing.
 * The skipped ones are counted so a caller can say so.
 *
 * @return the number of rows read; @p unknown, when given, takes the
 *         number of lines that were passed over.
 */
int tiku_form_parse(tiku_form_t *f, const char *text, int *unknown);

/** @brief How tall the described panel wants to be, at this face. */
int tiku_form_height(const tiku_form_t *f);

/** @brief Where row @p i falls when the form is drawn in @p body. */
tiku_rect_t tiku_form_row_rect(const tiku_form_t *f, tiku_rect_t body,
                               int i);

/** @brief Which row @p x, @p y lands on, or -1. */
int tiku_form_at(const tiku_form_t *f, tiku_rect_t body, int x, int y);

/**
 * @brief Draw the form in @p body.
 *
 * Every row records itself as the control it is, so a described panel
 * narrates like any other window -- which is the point: a board's
 * interface is readable by a person, a test and an agent without the
 * board knowing any of them exist.
 */
void tiku_form_draw(const tiku_form_t *f, tiku_surface_t *s,
                    tiku_rect_t body);

/**
 * @brief What working row @p i should WRITE to its bound place, or NULL
 *        when working it writes nothing.
 *
 * A toggle answers the other of 0 and 1; a button answers what it was
 * described with.  The caller does the writing, because only the caller
 * knows the namespace -- this file knows what the gesture MEANT.
 */
const char *tiku_form_press(tiku_form_t *f, int i);

#endif /* TIKU_FORM_H_ */
