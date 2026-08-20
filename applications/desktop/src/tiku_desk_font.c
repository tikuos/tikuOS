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

/* The faces handed out are mutable copies with a STABLE address, so a
 * caller that kept the pointer follows a size change instead of keeping
 * yesterday's metrics. */
static tiku_desk_font_t current_plain;
static tiku_desk_font_t current_bold;
static int current_size;

int
tiku_desk_font_set_size(int px)
{
    int i, best = 0, n = (int)(sizeof face_sizes / sizeof face_sizes[0]);

    for (i = 1; i < n; i++) {
        if (px >= face_sizes[i]) {
            best = i;
        }
    }
    current_plain = *plain_faces[best];
    current_bold = *bold_faces[best];
    current_size = face_sizes[best];
    return current_size;
}

int
tiku_desk_font_size(void)
{
    if (current_size == 0) {
        (void)tiku_desk_font_set_size(12);
    }
    return current_size;
}

const tiku_desk_font_t *
tiku_desk_font_plain(void)
{
    if (current_size == 0) {
        (void)tiku_desk_font_set_size(12);
    }
    return &current_plain;
}

const tiku_desk_font_t *
tiku_desk_font_at(int px)
{
    int i, best = 0, n = (int)(sizeof face_sizes / sizeof face_sizes[0]);

    if (current_size == 0) {
        (void)tiku_desk_font_set_size(12);
    }
    for (i = 1; i < n; i++) {
        if (px >= face_sizes[i]) {
            best = i;
        }
    }
    return plain_faces[best];
}

const tiku_desk_font_t *
tiku_desk_font_mono(int bold)
{
    /* Follows the interface size the user chose: the bigger the rest of
     * the desktop is, the bigger a terminal's characters are. */
    int i, best = 0, n = (int)(sizeof mono_sizes / sizeof mono_sizes[0]);

    if (current_size == 0) {
        (void)tiku_desk_font_set_size(12);
    }
    for (i = 1; i < n; i++) {
        if (current_size >= mono_sizes[i] - 1) {
            best = i;
        }
    }
    return bold ? monobold_faces[best] : mono_faces[best];
}

int
tiku_desk_font_mono_cell(int bold)
{
    return tiku_desk_text_width(tiku_desk_font_mono(bold), "M");
}

const tiku_desk_font_t *
tiku_desk_font_bold(void)
{
    if (current_size == 0) {
        (void)tiku_desk_font_set_size(12);
    }
    return &current_bold;
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

/** @brief Blend @p c over the NATIVE pixel at (x,y), coverage @p a. */
static void
blend(tiku_desk_surface_t *s, int sc, int x, int y, tiku_desk_rgb_t c,
      unsigned a)
{
    tiku_desk_rgb_t d;
    unsigned dr, dg, db, sr, sg, sb;
    long stride = (long)s->w * sc;

    if (a == 0u ||
        x < s->clip.x * sc || y < s->clip.y * sc ||
        x >= (s->clip.x + s->clip.w) * sc ||
        y >= (s->clip.y + s->clip.h) * sc) {
        return;
    }
    if (a >= 255u) {
        s->px[(long)y * stride + x] = c;
        return;
    }
    d = s->px[(long)y * stride + x];
    dr = (d >> 16) & 0xFFu; dg = (d >> 8) & 0xFFu; db = d & 0xFFu;
    sr = (c >> 16) & 0xFFu; sg = (c >> 8) & 0xFFu; sb = c & 0xFFu;
    dr = (sr * a + dr * (255u - a)) / 255u;
    dg = (sg * a + dg * (255u - a)) / 255u;
    db = (sb * a + db * (255u - a)) / 255u;
    s->px[(long)y * stride + x] = TIKU_DESK_RGB(dr, dg, db);
}

void
tiku_desk_text(tiku_desk_surface_t *s, const tiku_desk_font_t *f, int x, int y,
               const char *text, tiku_desk_rgb_t c)
{
    const tiku_desk_font_t *face;
    int sc, rep, pen;

    if (s == NULL || f == NULL || text == NULL) {
        return;
    }
    sc = (s->scale > 1) ? s->scale : 1;
    /* An even scale draws the 2x face: half the replication, twice the
     * detail.  The advances match by construction, so the layout the
     * logical metrics promised is exactly the space this ink fills. */
    face = f;
    rep = sc;
    if (sc > 1 && (sc & 1) == 0 && f->hi != NULL) {
        face = f->hi;
        rep = sc / 2;
    }
    pen = x * sc;
    for (; *text != '\0'; text++) {
        const tiku_desk_glyph_t *g = glyph_of(face, (unsigned char)*text);
        int gx, gy, rx, ry;

        for (gy = 0; gy < g->h; gy++) {
            const unsigned char *row = face->bits + g->off + (long)gy * g->w;
            for (gx = 0; gx < g->w; gx++) {
                int nx = pen + (g->ox + gx) * rep;
                int ny = y * sc + (g->oy + gy) * rep;

                for (ry = 0; ry < rep; ry++) {
                    for (rx = 0; rx < rep; rx++) {
                        blend(s, sc, nx + rx, ny + ry, c, row[gx]);
                    }
                }
            }
        }
        pen += g->adv * rep;
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
