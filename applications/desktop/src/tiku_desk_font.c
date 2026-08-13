/*
 * Tiku Desktop -- graphical interface to TikuOS devices.
 *
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 *
 * tiku_desk_font.c - glyph blending.
 *
 * Coverage is blended against the destination pixel rather than a supplied
 * background, so a label reads correctly wherever it lands.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include "tiku_desk_font.h"
#include "tiku_desk_font_data.h"

#include <string.h>

const tiku_desk_font_t *
tiku_desk_font_plain(void)
{
    return &plain_font;
}

const tiku_desk_font_t *
tiku_desk_font_bold(void)
{
    return &bold_font;
}

/** @brief Glyph for @p ch, or the space glyph when out of range. */
static const tiku_desk_glyph_t *
glyph_of(const tiku_desk_font_t *f, unsigned char ch)
{
    int i = (int)ch - f->first;

    if (i < 0 || i >= f->count) {
        i = 0;
    }
    return &f->glyphs[i];
}

int
tiku_desk_text_width(const tiku_desk_font_t *f, const char *text)
{
    int w = 0;

    if (f == NULL || text == NULL) {
        return 0;
    }
    for (; *text != '\0'; text++) {
        w += glyph_of(f, (unsigned char)*text)->adv;
    }
    return w;
}

int
tiku_desk_text_height(const tiku_desk_font_t *f)
{
    return (f != NULL) ? f->height : 0;
}

/** @brief Blend @p c over the pixel at (x,y) with coverage @p a (0..255). */
static void
blend(tiku_desk_surface_t *s, int x, int y, tiku_desk_rgb_t c, unsigned a)
{
    tiku_desk_rgb_t d;
    unsigned dr, dg, db, sr, sg, sb;

    if (a == 0u || s == NULL ||
        x < s->clip.x || y < s->clip.y ||
        x >= s->clip.x + s->clip.w || y >= s->clip.y + s->clip.h) {
        return;
    }
    if (a >= 255u) {
        s->px[(long)y * s->w + x] = c;
        return;
    }
    d = s->px[(long)y * s->w + x];
    dr = (d >> 16) & 0xFFu; dg = (d >> 8) & 0xFFu; db = d & 0xFFu;
    sr = (c >> 16) & 0xFFu; sg = (c >> 8) & 0xFFu; sb = c & 0xFFu;
    dr = (sr * a + dr * (255u - a)) / 255u;
    dg = (sg * a + dg * (255u - a)) / 255u;
    db = (sb * a + db * (255u - a)) / 255u;
    s->px[(long)y * s->w + x] = TIKU_DESK_RGB(dr, dg, db);
}

void
tiku_desk_text(tiku_desk_surface_t *s, const tiku_desk_font_t *f, int x, int y,
               const char *text, tiku_desk_rgb_t c)
{
    int pen = x;

    if (s == NULL || f == NULL || text == NULL) {
        return;
    }
    for (; *text != '\0'; text++) {
        const tiku_desk_glyph_t *g = glyph_of(f, (unsigned char)*text);
        int gx, gy;

        for (gy = 0; gy < g->h; gy++) {
            const unsigned char *row = f->bits + g->off + (long)gy * g->w;
            for (gx = 0; gx < g->w; gx++) {
                blend(s, pen + g->ox + gx, y + g->oy + gy, c, row[gx]);
            }
        }
        pen += g->adv;
    }
}

int
tiku_desk_text_centered(tiku_desk_surface_t *s, const tiku_desk_font_t *f,
                        tiku_desk_rect_t r, const char *text,
                        tiku_desk_rgb_t c)
{
    int tw = tiku_desk_text_width(f, text);
    int x = r.x + (r.w - tw) / 2;
    /* Centre the ink, not the line box: R5 labels sit optically centred. */
    int y = r.y + (r.h - f->height) / 2 + f->ascent;

    tiku_desk_text(s, f, x, y, text, c);
    return x;
}
