/*
 * TikuOS kits -- the foundation TikuDesktop and TikuTracker
 * both stand on.
 *
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 *
 * tiku_splash.c - painting the splash screen.
 *
 * SPDX-License-Identifier: Apache-2.0
 */
#include <string.h>

#include "tiku_font.h"
#include "tiku_logo.h"
#include "tiku_splash.h"
#include "tiku_ui.h"

/** @brief The artwork band's height, as a share of the splash screen's. */
static int
band_h(int h)
{
    return (h * 2) / 5;
}

/**
 * @brief The artwork: the palettes the mark is built from, swept across
 *        the band, with the mark large at the right of it.
 *
 * A sweep rather than a picture, because a picture has its colours
 * baked in and this band re-themes with the mark the day the palettes
 * do.  The mark sits where the old splash screens put their artwork's eye:
 * right of centre, large enough to be the point.
 */
static void
band_paint(tiku_surface_t *s, int w, int h)
{
    int sets = tiku_logo_palette_count();
    int x, y;

    for (x = 1; x < w - 1; x++) {
        float along = (float)x / (float)(w > 1 ? w - 1 : 1);
        float seat = along * (float)(sets > 1 ? sets - 1 : 1);
        tiku_logo_palette_t p;

        tiku_logo_palette((int)seat, seat - (float)(int)seat, &p);
        for (y = 1; y < h; y++) {
            /* The tint above, carried toward the accent below: a
             * ground and a sky, from the two colours the mark already
             * owns. */
            float down = (float)y / (float)h;
            int tr = (int)(p.tint >> 16) & 0xff;
            int tg = (int)(p.tint >> 8) & 0xff;
            int tb = (int)p.tint & 0xff;
            int ar = (int)(p.accent >> 16) & 0xff;
            int ag = (int)(p.accent >> 8) & 0xff;
            int ab = (int)p.accent & 0xff;

            tiku_pixel(s, x, y,
                TIKU_RGB(tr + (int)((ar - tr) * down),
                              tg + (int)((ag - tg) * down),
                              tb + (int)((ab - tb) * down)));
        }
    }
    {
        int mark = h - 14;
        tiku_rect_t at = { (w * 5) / 8, 8, mark, mark };

        tiku_logo_paint(s, at, 0u);
    }
    tiku_hline(s, 1, h, w - 2, tiku_tint(TIKU_C_PANEL, TIKU_DARKEN_2));
}

/**
 * @brief The person's own picture, scaled to COVER the band.
 *
 * Covered, not fitted: a band with bars down its sides is a picture
 * apologising for its shape.  What the crop loses at an edge the fill
 * of the band is worth.
 */
static void
art_paint(tiku_surface_t *s, int w, int h,
          const tiku_surface_t *art)
{
    int x, y;
    float scale, sw, sh;
    float off_x, off_y;

    if (art->w < 1 || art->h < 1) {
        return;
    }
    sw = (float)w / (float)art->w;
    sh = (float)h / (float)art->h;
    scale = (sw > sh) ? sw : sh;
    off_x = ((float)art->w - (float)w / scale) / 2.0f;
    off_y = ((float)art->h - (float)h / scale) / 2.0f;
    for (y = 1; y < h; y++) {
        for (x = 1; x < w - 1; x++) {
            int ax = (int)(off_x + (float)x / scale);
            int ay = (int)(off_y + (float)y / scale);

            if (ax < 0) { ax = 0; }
            if (ay < 0) { ay = 0; }
            if (ax >= art->w) { ax = art->w - 1; }
            if (ay >= art->h) { ay = art->h - 1; }
            tiku_pixel(s, x, y, art->px[ay * art->w + ax]);
        }
    }
    tiku_hline(s, 1, h, w - 2, tiku_tint(TIKU_C_PANEL, TIKU_DARKEN_2));
}

void
tiku_splash_paint(tiku_surface_t *s, const char *name,
                       const char *const *lines, int nlines,
                       const char *version, const char *status,
                       const tiku_surface_t *art)
{
    const tiku_font_t *big = tiku_font_at(26);
    const tiku_font_t *f = tiku_font_plain();
    tiku_rect_t all;
    int band, x, y, i;

    if (s == NULL || name == NULL) {
        return;
    }
    all = (tiku_rect_t){ 0, 0, s->w, s->h };
    band = band_h(s->h);
    x = 18;
    /* The splash screen is paper, edged once: a window that is not a window,
     * announcing that it will not be staying. */
    tiku_fill(s, all, TIKU_C_DOC);
    tiku_frame(s, all, tiku_tint(TIKU_C_PANEL, TIKU_DARKEN_2));
    if (art != NULL) {
        art_paint(s, s->w, band, art);
    } else {
        band_paint(s, s->w, band);
    }
    /* The name, big, at the left under the artwork. */
    y = band + 12;
    tiku_text(s, big, x, y + big->ascent, name, TIKU_C_TEXT);
    y += big->height + 6;
    /* What is being gathered, said right under the name -- the one part
     * of the old splash screens that was never decoration. */
    if (status != NULL) {
        tiku_text(s, f, x, y + f->ascent, status,
                       tiku_tint(TIKU_C_PANEL, TIKU_DARKEN_2));
    }
    y += f->height + 10;
    /* The small lines, as a block: who it is by, what it stands on. */
    for (i = 0; i < nlines && i < TIKU_SPLASH_LINES; i++) {
        if (lines[i] == NULL) {
            break;
        }
        tiku_text(s, f, x, y + f->ascent, lines[i],
                       tiku_tint(TIKU_C_PANEL, TIKU_DARKEN_2));
        y += f->height + 2;
    }
    /* The corners: the version at the left, the mark at the right --
     * both there whether or not anything is still being gathered. */
    if (version != NULL) {
        tiku_text(s, f, x, s->h - 10 - f->height + f->ascent, version,
                       tiku_tint(TIKU_C_PANEL, TIKU_DARKEN_1));
    }
    tiku_logo_paint(s, (tiku_rect_t){ s->w - 34, s->h - 34, 22, 22 },
                         0u);
}
