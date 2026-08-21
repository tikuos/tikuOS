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
static int current_family;

#define FAMILY_COUNT ((int)(sizeof family_names / sizeof family_names[0]))
#define SIZE_COUNT   ((int)(sizeof face_sizes / sizeof face_sizes[0]))

/** @brief The rung nearest @p px, snapping down. */
static int
rung_of(int px)
{
    int i, best = 0;

    for (i = 1; i < SIZE_COUNT; i++) {
        if (px >= face_sizes[i]) {
            best = i;
        }
    }
    return best;
}

/** @brief Put family @p family at rung @p rung into the faces handed out. */
static void
adopt(int family, int rung)
{
    current_plain = *plain_faces[family][rung];
    current_bold = *bold_faces[family][rung];
    current_family = family;
    current_size = face_sizes[rung];
}

int
tiku_desk_font_set_size(int px)
{
    adopt(current_family, rung_of(px));
    return current_size;
}

int
tiku_desk_font_family_count(void)
{
    return FAMILY_COUNT;
}

const char *
tiku_desk_font_family_name(int family)
{
    if (family < 0 || family >= FAMILY_COUNT) {
        return NULL;
    }
    return family_names[family];
}

int
tiku_desk_font_set_family(int family)
{
    if (current_size == 0) {
        (void)tiku_desk_font_set_size(12);
    }
    if (family >= 0 && family < FAMILY_COUNT) {
        adopt(family, rung_of(current_size));
    }
    return current_family;
}

int
tiku_desk_font_family(void)
{
    return current_family;
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
    if (current_size == 0) {
        (void)tiku_desk_font_set_size(12);
    }
    return plain_faces[current_family][rung_of(px)];
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

/**
 * @brief The next code point in @p text, stepping @p text past it.
 *
 * Names are UTF-8 -- a file called "caf\u00e9" is five bytes and four
 * letters -- so the text path walks code points, not bytes.  A malformed
 * sequence yields its lead byte rather than running off the end.
 */
static unsigned
utf8_next(const char **text)
{
    const unsigned char *p = (const unsigned char *)*text;
    unsigned cp = *p;
    int extra;

    if (cp < 0x80u) {
        extra = 0;
    } else if ((cp & 0xE0u) == 0xC0u) {
        cp &= 0x1Fu; extra = 1;
    } else if ((cp & 0xF0u) == 0xE0u) {
        cp &= 0x0Fu; extra = 2;
    } else if ((cp & 0xF8u) == 0xF0u) {
        cp &= 0x07u; extra = 3;
    } else {
        extra = 0;              /* a stray continuation byte stands alone */
    }
    while (extra-- > 0 && (p[1] & 0xC0u) == 0x80u) {
        cp = (cp << 6) | (unsigned)(*++p & 0x3Fu);
    }
    *text = (const char *)(p + 1);
    return cp;
}

/** @brief Whether @p f carries @p cp at all. */
static int
face_has(const tiku_desk_font_t *f, unsigned cp)
{
    long i = (long)cp - (long)f->first;

    return i >= 0 && i < (long)f->count;
}

/** @brief Glyph for @p cp, or the space glyph when it is not baked. */
static const tiku_desk_glyph_t *
glyph_of(const tiku_desk_font_t *f, unsigned cp)
{
    long i = face_has(f, cp) ? (long)cp - (long)f->first : 0;

    return &f->glyphs[i];
}

int
tiku_desk_text_width(const tiku_desk_font_t *f, const char *text)
{
    int w = 0;

    if (f == NULL || text == NULL) {
        return 0;
    }
    while (*text != '\0') {
        w += glyph_of(f, utf8_next(&text))->adv;
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
    const tiku_desk_font_t *hi;
    int sc, pen;

    if (s == NULL || f == NULL || text == NULL) {
        return;
    }
    sc = (s->scale > 1) ? s->scale : 1;
    /* An even scale draws the 2x face: half the replication, twice the
     * detail.  The advances match by construction, so the layout the
     * logical metrics promised is exactly the space this ink fills. */
    hi = (sc > 1 && (sc & 1) == 0) ? f->hi : NULL;
    pen = x * sc;
    while (*text != '\0') {
        unsigned cp = utf8_next(&text);
        const tiku_desk_glyph_t *logical = glyph_of(f, cp);
        const tiku_desk_font_t *face = f;
        const tiku_desk_glyph_t *g = logical;
        int gx, gy, rx, ry, rep = sc;

        /* The 2x face is a refinement, not a replacement: a letter it
         * does not carry is drawn from the 1x face replicated -- chunky,
         * where dropping to the 2x face's space glyph would be blank. */
        if (hi != NULL && face_has(hi, cp)) {
            face = hi;
            g = glyph_of(hi, cp);
            rep = sc / 2;
        }

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
        /* Always the logical advance: what the layout was measured
         * against, whichever face happened to draw the letter. */
        pen += logical->adv * sc;
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
