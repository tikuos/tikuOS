/*
 * Tiku Desktop -- graphical interface to TikuOS devices.
 *
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 *
 * tiku_desk_ui.c - the R5 control set, drawn from the spec table.
 *
 * Each control is bevel pairs around a flat face (R5-SPEC.md); the tints are
 * named as the BeOS constants so a control reads against the documentation.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include "tiku_desk_ui.h"

#include <string.h>

#define PANEL   TIKU_DESK_C_PANEL

/* The four shades every control is built from. */
#define WHITE()   tiku_desk_tint(PANEL, TIKU_DESK_LIGHTEN_MAX)
#define LIGHT()   tiku_desk_tint(PANEL, TIKU_DESK_LIGHTEN_1)
#define SHADOW()  tiku_desk_tint(PANEL, TIKU_DESK_DARKEN_2)
#define FRAME()   tiku_desk_tint(PANEL, TIKU_DESK_DARKEN_4)
#define DARK()    tiku_desk_tint(PANEL, 1.40f)

void
tiku_desk_ui_panel(tiku_desk_surface_t *s, tiku_desk_rect_t r)
{
    tiku_desk_fill(s, r, PANEL);
}

void
tiku_desk_ui_raised(tiku_desk_surface_t *s, tiku_desk_rect_t r)
{
    tiku_desk_fill(s, r, PANEL);
    tiku_desk_bevel(s, r, WHITE(), SHADOW());
}

void
tiku_desk_ui_sunken(tiku_desk_surface_t *s, tiku_desk_rect_t r,
                    tiku_desk_rgb_t face)
{
    tiku_desk_fill(s, r, face);
    /* Sunken is the raised bevel inverted: shadow above, light below. */
    tiku_desk_bevel(s, r, tiku_desk_tint(PANEL, TIKU_DESK_DARKEN_3), WHITE());
    tiku_desk_bevel(s, tiku_desk_inset(r, 1), DARK(), PANEL);
}

/**
 * @brief Knock the four corner pixels out of a control's outline.
 *
 * R5's buttons and fields are square with clipped corners rather than drawn
 * curves; one pixel is the whole radius.
 */
static void
clip_corners(tiku_desk_surface_t *s, tiku_desk_rect_t r, tiku_desk_rgb_t bg)
{
    tiku_desk_pixel(s, r.x, r.y, bg);
    tiku_desk_pixel(s, r.x + r.w - 1, r.y, bg);
    tiku_desk_pixel(s, r.x, r.y + r.h - 1, bg);
    tiku_desk_pixel(s, r.x + r.w - 1, r.y + r.h - 1, bg);
}

void
tiku_desk_ui_button(tiku_desk_surface_t *s, tiku_desk_rect_t r,
                    const char *label, unsigned state)
{
    tiku_desk_rect_t f = r;
    tiku_desk_rgb_t text = (state & TIKU_DESK_S_DISABLED)
                           ? tiku_desk_tint(PANEL, 1.30f)
                           : TIKU_DESK_C_TEXT;
    const tiku_desk_font_t *font = tiku_desk_font_plain();

    if (state & TIKU_DESK_S_DEFAULT) {
        /* The default button wears a ring with clear air between it and the
         * frame -- the gap is what makes it read as "this one". */
        tiku_desk_rect_t ring = r;
        tiku_desk_fill(s, ring, PANEL);
        tiku_desk_frame(s, ring, DARK());
        clip_corners(s, ring, PANEL);
        tiku_desk_bevel(s, tiku_desk_inset(ring, 1), SHADOW(), WHITE());
        f = tiku_desk_inset(r, 4);
    }

    tiku_desk_fill(s, f, PANEL);
    tiku_desk_frame(s, f, DARK());
    clip_corners(s, f, PANEL);

    if (state & TIKU_DESK_S_PRESSED) {
        /* Pressed reads as a hole: the face drops two steps and the bevel
         * inverts, so the state is legible without colour. */
        tiku_desk_rect_t in = tiku_desk_inset(f, 1);
        tiku_desk_fill(s, in, tiku_desk_tint(PANEL, 1.16f));
        tiku_desk_bevel(s, in, DARK(), LIGHT());
        tiku_desk_bevel(s, tiku_desk_inset(f, 2),
                        tiku_desk_tint(PANEL, TIKU_DESK_DARKEN_2), PANEL);
    } else {
        tiku_desk_bevel(s, tiku_desk_inset(f, 1), WHITE(), SHADOW());
        tiku_desk_bevel(s, tiku_desk_inset(f, 2), LIGHT(), PANEL);
    }

    if (state & TIKU_DESK_S_FOCUS) {
        tiku_desk_frame(s, tiku_desk_inset(f, 3), TIKU_DESK_C_FOCUS);
    }
    {
        tiku_desk_rect_t t = f;
        if (state & TIKU_DESK_S_PRESSED) {
            t.x += 1;
            t.y += 1;
        }
        (void)tiku_desk_text_centered(s, font, t, label, text);
    }
}

/** @brief The 13x13 well shared by checkbox and radio. */
static tiku_desk_rect_t
box_rect(tiku_desk_rect_t r)
{
    tiku_desk_rect_t b;

    b.w = 13;
    b.h = 13;
    b.x = r.x;
    b.y = r.y + (r.h - 13) / 2;
    return b;
}

/**
 * @brief One blended square: a diagonal gradient inside a dark outline.
 *
 * The gradient runs corner to corner, not top to bottom, which is why these
 * squares catch the light the way they do.
 */
static void
blended_rect(tiku_desk_surface_t *s, tiku_desk_rect_t r, tiku_desk_rgb_t face,
             int down)
{
    tiku_desk_rgb_t start = down ? tiku_desk_tint(face, TIKU_DESK_DARKEN_1)
                                 : tiku_desk_tint(face,
                                                  TIKU_DESK_LIGHTEN_MAX);
    tiku_desk_rgb_t end = down ? tiku_desk_tint(face, TIKU_DESK_LIGHTEN_1)
                               : face;
    tiku_desk_rect_t fill = tiku_desk_inset(r, 1);
    int x, y, span;

    if (r.w < 2 || r.h < 2) {
        return;
    }
    span = (fill.w - 1) + (fill.h - 1);
    if (span <= 0) {
        span = 1;
    }
    for (y = 0; y < fill.h; y++) {
        for (x = 0; x < fill.w; x++) {
            int t = ((x + y) * 255) / span;
            unsigned cr = (((start >> 16) & 0xFFu) * (255 - t) +
                           ((end >> 16) & 0xFFu) * t) / 255u;
            unsigned cg = (((start >> 8) & 0xFFu) * (255 - t) +
                           ((end >> 8) & 0xFFu) * t) / 255u;
            unsigned cb = ((start & 0xFFu) * (255 - t) +
                           (end & 0xFFu) * t) / 255u;

            tiku_desk_pixel(s, fill.x + x, fill.y + y,
                            TIKU_DESK_RGB(cr, cg, cb));
        }
    }
    tiku_desk_frame(s, r, tiku_desk_tint(face, TIKU_DESK_DARKEN_2));
}

void
tiku_desk_ui_tab_widget(tiku_desk_surface_t *s, tiku_desk_rect_t r,
                        int which, tiku_desk_rgb_t face, int down)
{
    if (which == TIKU_DESK_TAB_ZOOM) {
        /* Two squares from the same painter: the big one pushed down and
         * right by a quarter, the small one cut back by nearly a half. */
        int inset = r.w / 4;
        tiku_desk_rect_t big = { r.x + inset, r.y + inset, r.w - inset,
                                 r.h - inset };
        int cut = (int)((float)r.w / 2.1f);
        tiku_desk_rect_t small = { r.x, r.y, r.w - cut, r.h - cut };

        blended_rect(s, big, face, down);
        blended_rect(s, small, face, down);
        return;
    }
    blended_rect(s, r, face, down);
}

void
tiku_desk_ui_menufield(tiku_desk_surface_t *s, tiku_desk_rect_t r,
                       const char *label, unsigned state)
{
    const tiku_desk_font_t *font = tiku_desk_font_plain();
    tiku_desk_rgb_t text = (state & TIKU_DESK_S_DISABLED)
                           ? tiku_desk_tint(PANEL, 1.30f)
                           : TIKU_DESK_C_TEXT;
    int size, ax, ay, hin, vin, pen, i, y;

    tiku_desk_fill(s, r, PANEL);
    tiku_desk_frame(s, r, DARK());
    clip_corners(s, r, PANEL);
    if (state & TIKU_DESK_S_PRESSED) {
        tiku_desk_bevel(s, tiku_desk_inset(r, 1), SHADOW(), WHITE());
    } else {
        tiku_desk_bevel(s, tiku_desk_inset(r, 1), WHITE(), SHADOW());
        tiku_desk_bevel(s, tiku_desk_inset(r, 2), LIGHT(), PANEL);
    }
    if (state & TIKU_DESK_S_FOCUS) {
        tiku_desk_frame(s, tiku_desk_inset(r, 3), TIKU_DESK_C_FOCUS);
    }

    /* The label is clipped short of the marker rather than overrunning it. */
    size = (r.h * 2) / 3;
    ax = r.x + r.w - size - 5;
    tiku_desk_clip_set(s, (tiku_desk_rect_t){ r.x + 3, r.y, ax - r.x - 6,
                                              r.h });
    tiku_desk_text(s, font, r.x + 8,
                   r.y + (r.h - font->height) / 2 + font->ascent,
                   (label != NULL) ? label : "", text);
    tiku_desk_clip_reset(s);

    /* A stroked triangle, never filled: a solid one reads much heavier. */
    ay = r.y + (r.h - size) / 2;
    hin = size / 3;
    vin = size / 3;
    pen = (vin / 2 > 0) ? vin / 2 : 1;
    for (i = 0; i < pen; i++) {
        for (y = 0; y <= (size - 2 * hin) / 2; y++) {
            tiku_desk_pixel(s, ax + hin + y, ay + vin + y + i, DARK());
            tiku_desk_pixel(s, ax + size - hin - y, ay + vin + y + i, DARK());
        }
    }
}

void
tiku_desk_ui_checkbox(tiku_desk_surface_t *s, tiku_desk_rect_t r,
                      const char *label, unsigned state)
{
    tiku_desk_rect_t b = box_rect(r);
    tiku_desk_rgb_t text = (state & TIKU_DESK_S_DISABLED)
                           ? tiku_desk_tint(PANEL, 1.30f) : TIKU_DESK_C_TEXT;

    tiku_desk_fill(s, b, (state & TIKU_DESK_S_PRESSED)
                         ? tiku_desk_tint(PANEL, TIKU_DESK_DARKEN_1)
                         : TIKU_DESK_C_DOC);
    tiku_desk_bevel(s, b, DARK(), WHITE());
    tiku_desk_bevel(s, tiku_desk_inset(b, 1), SHADOW(), PANEL);

    if (state & TIKU_DESK_S_ON) {
        /* The R5 mark is a solid square, not a tick. */
        tiku_desk_rect_t m = tiku_desk_inset(b, 3);
        tiku_desk_fill(s, m, (state & TIKU_DESK_S_DISABLED)
                             ? tiku_desk_tint(PANEL, 1.30f)
                             : TIKU_DESK_C_TEXT);
    }
    if (state & TIKU_DESK_S_FOCUS) {
        tiku_desk_frame(s, (tiku_desk_rect_t){ b.x - 2, b.y - 2, b.w + 4,
                                               b.h + 4 }, TIKU_DESK_C_FOCUS);
    }
    if (label != NULL) {
        const tiku_desk_font_t *f = tiku_desk_font_plain();
        tiku_desk_text(s, f, b.x + b.w + 4,
                       r.y + (r.h - f->height) / 2 + f->ascent, label, text);
    }
}

void
tiku_desk_ui_radio(tiku_desk_surface_t *s, tiku_desk_rect_t r,
                   const char *label, unsigned state)
{
    tiku_desk_rect_t b = box_rect(r);
    tiku_desk_rgb_t text = (state & TIKU_DESK_S_DISABLED)
                           ? tiku_desk_tint(PANEL, 1.30f) : TIKU_DESK_C_TEXT;
    int cx = b.x + b.w / 2, cy = b.y + b.h / 2, rad = b.w / 2;
    int y;

    /* A 13 px circle drawn by spans: the radius is small enough that the
     * span table IS the antialiasing. */
    for (y = -rad; y <= rad; y++) {
        int half = (y * y >= rad * rad) ? 0
                 : (int)((rad * rad - y * y) / (rad * 0.9f) + 0.5f);
        int len = 2 * half + 1;
        tiku_desk_hline(s, cx - half, cy + y, len,
                        (state & TIKU_DESK_S_PRESSED)
                        ? tiku_desk_tint(PANEL, 1.16f)
                        : ((state & TIKU_DESK_S_DISABLED)
                           ? tiku_desk_tint(PANEL, TIKU_DESK_LIGHTEN_1)
                           : TIKU_DESK_C_DOC));
        if (y == -rad || y == rad) {
            tiku_desk_hline(s, cx - half, cy + y, len, DARK());
        } else {
            tiku_desk_pixel(s, cx - half - 1, cy + y, (y < 0) ? DARK()
                                                              : SHADOW());
            tiku_desk_pixel(s, cx + half + 1, cy + y, (y < 0) ? SHADOW()
                                                              : WHITE());
        }
    }
    if (state & TIKU_DESK_S_ON) {
        for (y = -2; y <= 2; y++) {
            int half = (y * y >= 4) ? 1 : 2;
            tiku_desk_hline(s, cx - half, cy + y, 2 * half + 1,
                            (state & TIKU_DESK_S_DISABLED)
                            ? tiku_desk_tint(PANEL, 1.30f)
                            : TIKU_DESK_C_TEXT);
        }
    }
    if (state & TIKU_DESK_S_FOCUS) {
        tiku_desk_frame(s, (tiku_desk_rect_t){ b.x - 2, b.y - 2, b.w + 4,
                                               b.h + 4 }, TIKU_DESK_C_FOCUS);
    }
    if (label != NULL) {
        const tiku_desk_font_t *f = tiku_desk_font_plain();
        tiku_desk_text(s, f, b.x + b.w + 4,
                       r.y + (r.h - f->height) / 2 + f->ascent, label, text);
    }
}

void
tiku_desk_ui_textfield(tiku_desk_surface_t *s, tiku_desk_rect_t r,
                       const char *text, int caret, unsigned state)
{
    tiku_desk_ui_textfield_sel(s, r, text, caret, 0, 0, state);
}

/** @brief Pixel offset of character @p at in @p text. */
static int
text_offset(const tiku_desk_font_t *f, const char *text, int at)
{
    char head[256];
    int n = (at < (int)sizeof head - 1) ? at : (int)sizeof head - 1;

    if (n <= 0) {
        return 0;
    }
    memcpy(head, text, (size_t)n);
    head[n] = '\0';
    return tiku_desk_text_width(f, head);
}

void
tiku_desk_ui_textfield_sel(tiku_desk_surface_t *s, tiku_desk_rect_t r,
                           const char *text, int caret, int sel_a, int sel_b,
                           unsigned state)
{
    tiku_desk_ui_textfield_scroll(s, r, text, caret, sel_a, sel_b, state,
                                  0);
}

void
tiku_desk_ui_textfield_scroll(tiku_desk_surface_t *s, tiku_desk_rect_t r,
                              const char *text, int caret, int sel_a,
                              int sel_b, unsigned state, int scroll_px)
{
    const tiku_desk_font_t *f = tiku_desk_font_plain();
    tiku_desk_rgb_t face = (state & TIKU_DESK_S_DISABLED)
                           ? PANEL : TIKU_DESK_C_DOC;
    int ty = r.y + (r.h - f->height) / 2 + f->ascent;

    tiku_desk_ui_sunken(s, r, face);
    if (state & TIKU_DESK_S_FOCUS) {
        tiku_desk_frame(s, r, TIKU_DESK_C_FOCUS);
    }
    tiku_desk_clip_set(s, tiku_desk_inset(r, 2));
    if (text != NULL) {
        int lo = (sel_a < sel_b) ? sel_a : sel_b;
        int hi = (sel_a < sel_b) ? sel_b : sel_a;

        if (hi > lo) {
            /* Reverse video under the range first, then the whole string in
             * one pass: drawing the text in three pieces would accumulate
             * the rounding of three separate advances. */
            tiku_desk_rect_t hl;

            hl.x = r.x + 3 - scroll_px + text_offset(f, text, lo);
            hl.y = r.y + 2;
            hl.w = text_offset(f, text, hi) - text_offset(f, text, lo);
            hl.h = r.h - 4;
            tiku_desk_fill(s, hl, TIKU_DESK_C_SELECT);
        }
        tiku_desk_text(s, f, r.x + 3 - scroll_px, ty, text,
                       (state & TIKU_DESK_S_DISABLED)
                       ? tiku_desk_tint(PANEL, 1.30f) : TIKU_DESK_C_TEXT);
        if (caret >= 0) {
            tiku_desk_vline(s, r.x + 3 - scroll_px + text_offset(f, text, caret),
                            r.y + 3, r.h - 6, TIKU_DESK_C_TEXT);
        }
    }
    tiku_desk_clip_reset(s);
}

/** @brief A scrollbar arrow button with its triangle. */
static void
arrow_button(tiku_desk_surface_t *s, tiku_desk_rect_t r, int dir, int can)
{
    int cx = r.x + r.w / 2, cy = r.y + r.h / 2, i;
    /* An arrow that cannot scroll any further says so by dimming: the
     * glyph fades toward the panel while the button itself stays. */
    tiku_desk_rgb_t ink = can ? DARK()
                              : tiku_desk_tint(DARK(), TIKU_DESK_LIGHTEN_2);

    tiku_desk_fill(s, r, PANEL);
    tiku_desk_bevel(s, r, WHITE(), SHADOW());
    tiku_desk_frame(s, r, FRAME());
    for (i = 0; i < 4; i++) {
        int len = 2 * i + 1;
        switch (dir) {
        case 0: tiku_desk_hline(s, cx - i, cy - 2 + i, len, ink); break;
        case 1: tiku_desk_hline(s, cx - i, cy + 2 - i, len, ink); break;
        case 2: tiku_desk_vline(s, cx - 2 + i, cy - i, len, ink); break;
        default: tiku_desk_vline(s, cx + 2 - i, cy - i, len, ink); break;
        }
    }
}

void
tiku_desk_ui_scrollbar(tiku_desk_surface_t *s, tiku_desk_rect_t r, float pos,
                       float frac, int horiz)
{
    int btn = horiz ? r.h : r.w;
    tiku_desk_rect_t track, thumb;
    int span, tlen, toff, i;

    tiku_desk_fill(s, r, tiku_desk_tint(PANEL, TIKU_DESK_DARKEN_1));
    tiku_desk_bevel(s, r, SHADOW(), WHITE());

    {
        /* Whether each direction HAS anywhere to go: at an end the arrow
         * pointing past it dims, and when everything fits both do. */
        int can_back = frac < 1.0f && pos > 0.0f;
        int can_on = frac < 1.0f && pos < 1.0f;

        if (horiz) {
            arrow_button(s, (tiku_desk_rect_t){ r.x, r.y, btn, btn }, 2,
                         can_back);
            arrow_button(s, (tiku_desk_rect_t){ r.x + r.w - btn, r.y, btn,
                                                btn }, 3, can_on);
            track = (tiku_desk_rect_t){ r.x + btn, r.y, r.w - 2 * btn, r.h };
            span = track.w;
        } else {
            arrow_button(s, (tiku_desk_rect_t){ r.x, r.y, btn, btn }, 0,
                         can_back);
            arrow_button(s, (tiku_desk_rect_t){ r.x, r.y + r.h - btn, btn,
                                                btn }, 1, can_on);
            track = (tiku_desk_rect_t){ r.x, r.y + btn, r.w, r.h - 2 * btn };
            span = track.h;
        }
    }
    if (frac > 1.0f) { frac = 1.0f; }
    if (frac < 0.05f) { frac = 0.05f; }
    tlen = (int)((float)span * frac);
    toff = (int)((float)(span - tlen) * pos);
    thumb = horiz
            ? (tiku_desk_rect_t){ track.x + toff, track.y, tlen, track.h }
            : (tiku_desk_rect_t){ track.x, track.y + toff, track.w, tlen };

    tiku_desk_fill(s, thumb, PANEL);
    tiku_desk_bevel(s, thumb, WHITE(), SHADOW());
    tiku_desk_frame(s, thumb, FRAME());

    /* The knurl: three grip lines at the thumb's centre, the detail that
     * makes an R5 scrollbar recognisable at a glance. */
    for (i = -1; i <= 1; i++) {
        if (horiz) {
            int x = thumb.x + thumb.w / 2 + i * 3;
            tiku_desk_vline(s, x, thumb.y + 4, thumb.h - 8, SHADOW());
            tiku_desk_vline(s, x + 1, thumb.y + 4, thumb.h - 8, WHITE());
        } else {
            int y = thumb.y + thumb.h / 2 + i * 3;
            tiku_desk_hline(s, thumb.x + 4, y, thumb.w - 8, SHADOW());
            tiku_desk_hline(s, thumb.x + 4, y + 1, thumb.w - 8, WHITE());
        }
    }
}

void
tiku_desk_ui_menubar(tiku_desk_surface_t *s, tiku_desk_rect_t r,
                     const char *const *items, int n, int active)
{
    const tiku_desk_font_t *f = tiku_desk_font_plain();
    int x = r.x + 6, i;

    tiku_desk_fill(s, r, PANEL);
    tiku_desk_hline(s, r.x, r.y + r.h - 1, r.w, SHADOW());
    for (i = 0; i < n; i++) {
        int w = tiku_desk_text_width(f, items[i]) + 16;
        tiku_desk_rect_t it = { x, r.y, w, r.h - 1 };
        int on = (i == active);

        if (on) {
            tiku_desk_fill(s, it, TIKU_DESK_C_SELECT);
        }
        (void)tiku_desk_text_centered(s, f, it, items[i],
                                      on ? TIKU_DESK_C_SELTEXT
                                         : TIKU_DESK_C_TEXT);
        x += w;
    }
}

void
tiku_desk_ui_menu_icons(tiku_desk_surface_t *s, tiku_desk_rect_t r,
                        const char *const *items, int n, int hot,
                        tiku_desk_ui_menu_icon_fn icon, void *context)
{
    const tiku_desk_font_t *f = tiku_desk_font_plain();
    int rowh = f->height + 6;
    int indent = (icon != NULL) ? 20 : 0;
    int i;

    tiku_desk_fill(s, r, PANEL);
    tiku_desk_bevel(s, r, WHITE(), SHADOW());
    tiku_desk_frame(s, r, FRAME());
    for (i = 0; i < n; i++) {
        tiku_desk_rect_t row = { r.x + 2, r.y + 2 + i * rowh, r.w - 4, rowh };
        int on = (i == hot);

        if (items[i][0] == '-') {          /* separator */
            int y = row.y + rowh / 2;
            tiku_desk_hline(s, row.x + 1, y, row.w - 2, DARK());
            tiku_desk_hline(s, row.x + 1, y + 1, row.w - 2, WHITE());
            continue;
        }
        if (on) {
            tiku_desk_fill(s, row, TIKU_DESK_C_SELECT);
        }
        if (icon != NULL) {
            icon(s, i, row.x + 4, row.y + (rowh - 16) / 2, 16, context);
        }
        /* The row is the label's world: a long one ends at the border
         * instead of walking out of the menu. */
        tiku_desk_clip_set(s, row);
        tiku_desk_text(s, f, row.x + 8 + indent,
                       row.y + (rowh - f->height) / 2 + f->ascent, items[i],
                       on ? TIKU_DESK_C_SELTEXT : TIKU_DESK_C_TEXT);
        tiku_desk_clip_reset(s);
    }
}

void
tiku_desk_ui_menu(tiku_desk_surface_t *s, tiku_desk_rect_t r,
                  const char *const *items, int n, int hot)
{
    tiku_desk_ui_menu_icons(s, r, items, n, hot, NULL, NULL);
}

void
tiku_desk_ui_list_row(tiku_desk_surface_t *s, tiku_desk_rect_t r,
                      const char *text, int selected)
{
    const tiku_desk_font_t *f = tiku_desk_font_plain();

    tiku_desk_fill(s, r, selected ? TIKU_DESK_C_SELECT : TIKU_DESK_C_DOC);
    tiku_desk_text(s, f, r.x + 4, r.y + (r.h - f->height) / 2 + f->ascent,
                   text, selected ? TIKU_DESK_C_SELTEXT : TIKU_DESK_C_TEXT);
}

void
tiku_desk_ui_list_header(tiku_desk_surface_t *s, tiku_desk_rect_t r,
                         const char *const *cols, const int *widths, int n,
                         int sort_col)
{
    const tiku_desk_font_t *f = tiku_desk_font_plain();
    int x = r.x, i;

    for (i = 0; i < n; i++) {
        tiku_desk_rect_t c = { x, r.y, widths[i], r.h };

        tiku_desk_fill(s, c, PANEL);
        tiku_desk_bevel(s, c, WHITE(), SHADOW());
        tiku_desk_text(s, f, c.x + 5, c.y + (c.h - f->height) / 2 + f->ascent,
                       cols[i], TIKU_DESK_C_TEXT);
        if (i == sort_col) {
            /* Sort marker: the same triangle the scrollbar arrows use. */
            int cx = c.x + c.w - 10, cy = c.y + c.h / 2, k;
            for (k = 0; k < 3; k++) {
                tiku_desk_hline(s, cx - k, cy + 1 - k, 2 * k + 1, DARK());
            }
        }
        x += widths[i];
    }
}

tiku_desk_rect_t
tiku_desk_ui_window(tiku_desk_surface_t *s, tiku_desk_rect_t r,
                    const char *title, int active)
{
    const tiku_desk_font_t *f = tiku_desk_font_bold();
    const int tabh = 21, border = 5;
    int tabw = tiku_desk_text_width(f, title) + 24;
    tiku_desk_rect_t tab = { r.x + 4, r.y, tabw, tabh };
    tiku_desk_rect_t body = { r.x, r.y + tabh - 1, r.w, r.h - tabh + 1 };
    tiku_desk_rect_t content;
    tiku_desk_rgb_t tabc = active ? TIKU_DESK_C_TAB : TIKU_DESK_C_TAB_IDLE;

    if (tabw > r.w - 8) {
        tabw = r.w - 8;
        tab.w = tabw;
    }

    /* Body frame first, so the tab overlaps its top edge like R5's does. */
    tiku_desk_fill(s, body, PANEL);
    tiku_desk_frame(s, body, DARK());
    tiku_desk_bevel(s, tiku_desk_inset(body, 1), WHITE(), SHADOW());

    tiku_desk_fill(s, tab, tabc);
    tiku_desk_frame(s, tab, DARK());
    tiku_desk_bevel(s, tiku_desk_inset(tab, 1),
                    tiku_desk_tint(tabc, TIKU_DESK_LIGHTEN_1),
                    tiku_desk_tint(tabc, TIKU_DESK_DARKEN_2));
    /* The tab's bottom edge is the body's top: erase the line between. */
    tiku_desk_hline(s, tab.x + 1, tab.y + tab.h - 1, tab.w - 2, tabc);
    tiku_desk_hline(s, tab.x + 1, tab.y + tab.h, tab.w - 2, PANEL);
    tiku_desk_text(s, f, tab.x + 12,
                   tab.y + (tabh - f->height) / 2 + f->ascent, title,
                   TIKU_DESK_C_TEXT);

    content.x = body.x + border;
    content.y = body.y + border;
    content.w = body.w - 2 * border;
    content.h = body.h - 2 * border;
    return content;
}
