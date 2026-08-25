/*
 * TikuOS kits -- the foundation TikuDesktop and TikuTracker
 * both stand on.
 *
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 *
 * tiku_alert.h - the question put to the user.
 *
 * A text, a kind, and a row of buttons one of which is pressed: state,
 * layout, hit-testing, keys and the look, so an application that has to
 * ask -- save these changes?  replace that file? -- holds one of these
 * over its own content instead of growing a dialog system of its own.
 *
 * Grown in Tracker, where the engine had always been able to ask and the
 * shells answered for it; moved here because the QUESTION is generic.
 * What stays behind is what the answers mean -- an answer's meaning
 * belongs to whoever asked.
 *
 * SPDX-License-Identifier: Apache-2.0
 */
#ifndef TIKU_ALERT_H_
#define TIKU_ALERT_H_

#include "tiku_gfx.h"

/** @brief How many buttons an alert can carry. */
#define TIKU_ALERT_BUTTONS 4

/** @brief What an alert is for; it decides the icon and the wording. */
typedef enum {
    TIKU_ALERT_INFO = 0,
    TIKU_ALERT_WARN,
    TIKU_ALERT_STOP
} tiku_alert_kind_t;

/** @brief One alert, open or not. */
typedef struct {
    int               open;
    tiku_alert_kind_t kind;
    char              text[512];
    char              button[TIKU_ALERT_BUTTONS][40];
    int               nbutton;
    int               cancel;   /* which button Escape presses       */
    /* Which button Return presses, drawn with the default ring.  The
     * rightmost unless the rightmost is DESTRUCTIVE: a default that
     * erases something must be an aimed click, never a keyboard reflex
     * (Mac HIG ch3 / NeXT UIG ch5). */
    int               dflt;
    /* Button width for THIS alert, measured from its longest label by the
     * renderer (which owns the fonts); 0 falls back to the classic fixed
     * width.  One width for the whole set: uniform buttons, sized by the
     * longest label (Mac HIG ch3). */
    int               btn_w;
    int               chosen;   /* -1 until one is pressed           */
    /* What the answer is FOR.  An alert raised over one question has to
     * hand its answer back to it, and the asker must not have to
     * remember which question it asked.  Opaque here: the values are the
     * asker's own. */
    int               tag;
} tiku_alert_t;

void tiku_alert_reset(tiku_alert_t *a);

/**
 * @brief Raise an alert.  @p buttons is a "|"-separated list, right to
 * left as the original lays them out: the default is last.
 */
void tiku_alert_open(tiku_alert_t *a, tiku_alert_kind_t kind, int tag,
                     const char *text, const char *buttons);

/** @brief The classic panel size, for a caller placing one. */
void tiku_alert_size(int *w, int *h);

/** @brief The rectangle of button @p i inside a @p w by @p h alert. */
void tiku_alert_button_rect(const tiku_alert_t *a, int i, int w, int h,
                            int *bx, int *by, int *bw, int *bh);

/** @brief Which button a point is over, or -1. */
int tiku_alert_button_at(const tiku_alert_t *a, int w, int h, int x, int y);

/**
 * @brief Press the button a key stands for.
 *
 * Return presses the default and Escape the cancel route.  @return the
 * button pressed, recorded in @p a->chosen as a click would be, or -1
 * when the key was not the alert's to take.
 */
int tiku_alert_key(tiku_alert_t *a, unsigned key);

/**
 * @brief Draw the whole panel into @p frame: chrome, stripe, icon, the
 * two-tier text, and the buttons with the default ringed.
 *
 * Also measures the button width for this alert's labels into
 * @p a->btn_w, so a hit-test that follows agrees with what was drawn.
 */
void tiku_alert_draw(tiku_alert_t *a, tiku_surface_t *s,
                     tiku_rect_t frame);

#endif /* TIKU_ALERT_H_ */
