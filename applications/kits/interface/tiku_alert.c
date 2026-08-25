/*
 * TikuOS kits -- the foundation TikuDesktop and TikuTracker
 * both stand on.
 *
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 *
 * tiku_alert.c - the question's state, geometry and look.
 *
 * SPDX-License-Identifier: Apache-2.0
 */
#include <stdio.h>
#include <string.h>

#include "tiku_alert.h"
#include "tiku_dl.h"
#include "tiku_event.h"
#include "tiku_font.h"
#include "tiku_ui.h"

#define ALERT_WIDTH  420
#define ALERT_HEIGHT 150

#define BTN_W   96
#define BTN_H   24
#define BTN_GAP 8
#define BTN_PAD 12

void
tiku_alert_reset(tiku_alert_t *a)
{
    if (a != NULL) {
        memset(a, 0, sizeof *a);
        a->chosen = -1;
        a->cancel = -1;
    }
}

void
tiku_alert_open(tiku_alert_t *a, tiku_alert_kind_t kind, int tag,
                const char *text, const char *buttons)
{
    const char *p = buttons;

    if (a == NULL) {
        return;
    }
    tiku_alert_reset(a);
    a->kind = kind;
    a->tag = tag;
    snprintf(a->text, sizeof a->text, "%s", (text != NULL) ? text : "");
    while (p != NULL && *p != '\0' && a->nbutton < TIKU_ALERT_BUTTONS) {
        const char *bar = strchr(p, '|');
        size_t n = (bar != NULL) ? (size_t)(bar - p) : strlen(p);

        if (n >= sizeof a->button[0]) {
            n = sizeof a->button[0] - 1u;
        }
        memcpy(a->button[a->nbutton], p, n);
        a->button[a->nbutton][n] = '\0';
        a->nbutton++;
        p = (bar != NULL) ? bar + 1 : p + n;
    }
    /* Escape presses the FIRST button.  The original puts Cancel leftmost
     * and makes it the escape route, so the safe answer is the one a
     * keyboard reflex reaches. */
    a->cancel = (a->nbutton > 0) ? 0 : -1;
    a->dflt = a->nbutton - 1;
    {
        /* A destructive verb never rides Return: while the default names
         * an action that erases or overwrites, it steps LEFT toward the
         * safer answers -- the delete question's Return lands on Move to
         * Trash, an overwrite's on Cancel -- and the destructive answer
         * takes an aimed click (Mac HIG ch3; NeXT UIG ch5: the default
         * should be the safest of the alternatives). */
        static const char *harm[] = { "Delete", "Replace", "Replace all",
                                      "Empty Trash" };
        int harmful = 1;

        while (harmful && a->dflt > 0) {
            size_t k;

            harmful = 0;
            for (k = 0; k < sizeof harm / sizeof harm[0]; k++) {
                if (strcmp(a->button[a->dflt], harm[k]) == 0) {
                    harmful = 1;
                }
            }
            if (harmful) {
                a->dflt--;
            }
        }
    }
    a->open = 1;
}

void
tiku_alert_size(int *w, int *h)
{
    if (w != NULL) { *w = ALERT_WIDTH; }
    if (h != NULL) { *h = ALERT_HEIGHT; }
}

void
tiku_alert_button_rect(const tiku_alert_t *a, int i, int w, int h,
                       int *bx, int *by, int *bw, int *bh)
{
    int from_right;

    if (a == NULL || i < 0 || i >= a->nbutton) {
        return;
    }
    /* Laid out from the right edge: the default action is the rightmost,
     * which is where the pointer already is after reading the text. */
    from_right = a->nbutton - 1 - i;
    {
        /* One width for the whole set, from the longest label when the
         * renderer has measured one (Mac HIG ch3: uniform buttons, sized
         * by the longest). */
        int bwidth = (a->btn_w > 0) ? a->btn_w : BTN_W;

        if (bx != NULL) {
            *bx = w - BTN_PAD - bwidth - from_right * (bwidth + BTN_GAP);
        }
        if (bw != NULL) { *bw = bwidth; }
    }
    if (by != NULL) { *by = h - BTN_PAD - BTN_H; }
    if (bh != NULL) { *bh = BTN_H; }
}

int
tiku_alert_button_at(const tiku_alert_t *a, int w, int h, int x, int y)
{
    int i;

    if (a == NULL || !a->open) {
        return -1;
    }
    for (i = 0; i < a->nbutton; i++) {
        int bx, by, bw, bh;

        tiku_alert_button_rect(a, i, w, h, &bx, &by, &bw, &bh);
        if (x >= bx && x < bx + bw && y >= by && y < by + bh) {
            return i;
        }
    }
    return -1;
}

int
tiku_alert_key(tiku_alert_t *a, unsigned key)
{
    int at = -1;

    if (a == NULL || !a->open) {
        return -1;
    }
    if (key == TIKU_KEY_RETURN) {
        at = a->dflt;
    } else if (key == TIKU_KEY_ESCAPE) {
        at = a->cancel;
    }
    if (at >= 0) {
        a->chosen = at;
    }
    return at;
}

/** @brief A filled disc, span by span: the primitives have no circle. */
static void
draw_disc(tiku_surface_t *surface, int cx, int cy, int r, tiku_rgb_t color)
{
    int dy;

    for (dy = -r; dy <= r; dy++) {
        int half = 0;

        while ((half + 1) * (half + 1) + dy * dy <= r * r) {
            half++;
        }
        tiku_hline(surface, cx - half, cy + dy, 2 * half + 1, color);
    }
}

/**
 * @brief The alert's own icon: what KIND of thing is being said.
 *
 * Info speaks in blue, a warning in the tab's yellow, a stop in red --
 * a ringed disc with its mark, drawn from primitives so every target
 * carries it.
 */
void
tiku_alert_icon_draw(tiku_surface_t *surface, int cx, int cy,
                     tiku_alert_kind_t kind)
{
    int rec = tiku_gfx_rec_enter(surface);

    if (rec) {
        (void)tiku_dl_alert_icon(surface->record, cx, cy, (int)kind);
    }

    tiku_rgb_t body = (kind == TIKU_ALERT_STOP)
                               ? TIKU_C_DANGER
                         : (kind == TIKU_ALERT_WARN)
                               ? TIKU_C_TAB
                               : TIKU_C_SELECT;
    tiku_rgb_t ink = (kind == TIKU_ALERT_WARN)
                              ? TIKU_C_TEXT : TIKU_C_DOC;
    int k;

    draw_disc(surface, cx, cy, 16, tiku_tint(body, 1.45f));
    draw_disc(surface, cx, cy, 14, body);
    if (kind == TIKU_ALERT_STOP) {
        /* The cross: this far and no further. */
        for (k = -6; k <= 6; k++) {
            tiku_fill(surface, (tiku_rect_t){ cx + k - 1,
                           cy + k - 1, 3, 3 }, ink);
            tiku_fill(surface, (tiku_rect_t){ cx - k - 1,
                           cy + k - 1, 3, 3 }, ink);
        }
    } else if (kind == TIKU_ALERT_WARN) {
        /* The exclamation: a bar and its dot. */
        tiku_fill(surface, (tiku_rect_t){ cx - 2, cy - 9, 4, 11 }, ink);
        tiku_fill(surface, (tiku_rect_t){ cx - 2, cy + 5, 4, 4 }, ink);
    } else {
        /* The i: a dot and its bar. */
        tiku_fill(surface, (tiku_rect_t){ cx - 2, cy - 9, 4, 4 }, ink);
        tiku_fill(surface, (tiku_rect_t){ cx - 2, cy - 2, 4, 11 }, ink);
    }
    tiku_gfx_rec_leave(surface, rec);
}

void
tiku_alert_draw(tiku_alert_t *a, tiku_surface_t *s, tiku_rect_t frame)
{
    const tiku_font_t *font = tiku_font_plain();
    int i, text_y;

    if (a == NULL || s == NULL || !a->open) {
        return;
    }
    /* A modal window, not a floating slab: the same dark frame and
     * bevel every window body wears -- BeOS had no borderless windows,
     * and neither does this one. */
    tiku_fill(s, frame, TIKU_C_PANEL);
    tiku_frame(s, frame, tiku_tint(TIKU_C_PANEL, 1.40f));
    tiku_bevel(s, tiku_inset(frame, 1),
        tiku_tint(TIKU_C_PANEL, TIKU_LIGHTEN_MAX),
        tiku_tint(TIKU_C_PANEL, TIKU_DARKEN_2));
    tiku_bevel(s, tiku_inset(frame, 2),
        tiku_tint(TIKU_C_PANEL, TIKU_LIGHTEN_1),
        tiku_tint(TIKU_C_PANEL, TIKU_DARKEN_1));
    {
        /* The icon on its grey stripe, as BAlert lays it: the KIND of
         * thing being said, visible before a word is read. */
        tiku_rect_t stripe = { frame.x + 3, frame.y + 3, 52,
                                    frame.h - 6 };

        tiku_fill(s, stripe, tiku_tint(TIKU_C_PANEL, TIKU_DARKEN_1));
        tiku_alert_icon_draw(s, stripe.x + 26, frame.y + 36, a->kind);
    }
    text_y = frame.y + 24;
    {
        /* Two tiers (Mac HIG ch3): the first sentence is the SUMMARY and
         * wears the bold face; whatever follows is the detail, in the
         * plain face below it.  One text block in, two voices out -- no
         * caller changes. */
        const tiku_font_t *bold = tiku_font_bold();
        const char *text = a->text;
        const char *split = NULL;
        const char *at;
        size_t lines = 0;
        int tier;

        for (at = text; *at != '\0'; at++) {
            if ((*at == '.' || *at == '?' || *at == '!') &&
                (at[1] == ' ' || at[1] == '\0')) {
                split = at + 1;
                break;
            }
        }
        for (tier = 0; tier < 2; tier++) {
            const tiku_font_t *face = (tier == 0 && split != NULL)
                                               ? bold : font;
            const char *end = (tier == 0) ? split : NULL;
            char line[80] = "";

            if (tier == 1) {
                if (split == NULL || *split == '\0') {
                    break;
                }
                text = split;
                while (*text == ' ') {
                    text++;
                }
                text_y += 2;    /* a breath between the voices */
            }
            while (*text != '\0' && (end == NULL || text < end) &&
                   text_y < frame.y + frame.h - 46) {
                size_t length = 0;
                const char *space = NULL;

                while (text[length] != '\0' &&
                       (end == NULL || text + length < end) &&
                       length < sizeof line - 1u) {
                    if (text[length] == ' ') {
                        space = text + length;
                    }
                    line[length] = text[length];
                    line[length + 1u] = '\0';
                    if (tiku_text_width(face, line) > frame.w - 88) {
                        break;
                    }
                    length++;
                }
                if (text[length] != '\0' &&
                    (end == NULL || text + length < end) &&
                    space != NULL && space > text) {
                    length = (size_t)(space - text);
                    line[length] = '\0';
                }
                tiku_text(s, face, frame.x + 66, text_y, line,
                               TIKU_C_TEXT);
                text_y += face->height + 4;
                text += length;
                while (*text == ' ') {
                    text++;
                }
                if (++lines > 6u) {
                    break;
                }
            }
            if (split == NULL) {
                break;
            }
        }
    }
    {
        /* Uniform buttons, sized by the longest label (Mac HIG ch3): the
         * geometry lives beside the layout, the fonts live here, so the
         * width is measured here and written back for the hit-test. */
        int widest = 0;

        for (i = 0; i < a->nbutton; i++) {
            int lw = tiku_text_width(font, a->button[i]);

            if (lw > widest) {
                widest = lw;
            }
        }
        widest += 20;
        if (widest < 58) { widest = 58; }
        if (widest > 150) { widest = 150; }
        a->btn_w = widest;
    }
    for (i = 0; i < a->nbutton; i++) {
        tiku_rect_t button;
        int x, y, width, height;

        tiku_alert_button_rect(a, i, frame.w, frame.h,
                               &x, &y, &width, &height);
        button = (tiku_rect_t){ frame.x + x, frame.y + y, width, height };
        /* The R5 button, ring and all: the rightmost is the default,
         * which is where the pointer already is after reading. */
        tiku_ui_button(s, button, a->button[i],
                            (i == a->dflt) ? TIKU_S_DEFAULT : 0u);
    }
}
