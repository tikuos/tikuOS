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
#include "tiku_desk_ttf.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* The faces handed out are mutable copies with a STABLE address, so a
 * caller that kept the pointer follows a size change instead of keeping
 * yesterday's metrics. */
static tiku_desk_font_t current_plain;
static tiku_desk_font_t current_bold;
static int current_size;
static int current_family;

/*
 * A face read from a file.  The baked tables cannot hold one -- they are
 * const, and a dropped face is not known until somebody drops it -- so a
 * built face owns its glyphs and coverage and is rebuilt when the size
 * changes.  Latin-1, as the baked faces carry: the same letters, from a
 * different file.
 */
#define BUILT_FIRST 32
#define BUILT_LAST  0xFF
#define BUILT_COUNT (BUILT_LAST - BUILT_FIRST + 1)
#define BUILT_MAX   6           /* faces alive at once: sizes x weights */

typedef struct {
    tiku_desk_font_t   face;
    tiku_desk_glyph_t *glyphs;
    unsigned char     *bits;
    int                px, bold, live;
} built_face_t;

static built_face_t built[BUILT_MAX];
static tiku_desk_ttf_t *file_regular;
static tiku_desk_ttf_t *file_bold;
static char file_family[64];

/** @brief Give back what one built face holds. */
static void
built_free(built_face_t *b)
{
    free(b->glyphs);
    free(b->bits);
    memset(b, 0, sizeof *b);
}

/**
 * @brief Rasterise every letter of @p ttf at @p px into @p b.
 *
 * @return 1 when the face is usable.
 */
static int
built_make(built_face_t *b, const tiku_desk_ttf_t *ttf, int px, int bold)
{
    size_t used = 0, room = 4096u;
    unsigned char *bits = malloc(room);
    tiku_desk_glyph_t *glyphs = calloc(BUILT_COUNT, sizeof *glyphs);
    int ascent = 0, height = 0, cp;

    if (bits == NULL || glyphs == NULL) {
        free(bits);
        free(glyphs);
        return 0;
    }
    for (cp = BUILT_FIRST; cp <= BUILT_LAST; cp++) {
        tiku_desk_ttf_glyph_t g;
        tiku_desk_glyph_t *slot = &glyphs[cp - BUILT_FIRST];

        memset(&g, 0, sizeof g);
        if (!tiku_desk_ttf_render(ttf, (unsigned)cp, px, &g)) {
            /* Not in the file: no ink and no room, like a control. */
            slot->cp = (short)cp;
            slot->w = 1;
            slot->h = 1;
            slot->off = 0;
            continue;
        }
        slot->cp = (short)cp;
        slot->adv = (short)g.adv;
        slot->w = (short)((g.w > 0) ? g.w : 1);
        slot->h = (short)((g.h > 0) ? g.h : 1);
        slot->ox = (short)g.ox;
        slot->oy = (short)g.oy;
        slot->off = (int)used;
        {
            size_t need = (size_t)slot->w * (size_t)slot->h;

            while (used + need > room) {
                unsigned char *grown = realloc(bits, room * 2u);

                if (grown == NULL) {
                    tiku_desk_ttf_free_glyph(&g);
                    free(bits);
                    free(glyphs);
                    return 0;
                }
                bits = grown;
                room *= 2u;
            }
            if (g.cover != NULL) {
                memcpy(bits + used, g.cover, need);
            } else {
                memset(bits + used, 0, need);
            }
            used += need;
        }
        tiku_desk_ttf_free_glyph(&g);
    }
    tiku_desk_ttf_metrics(ttf, px, &ascent, &height);
    b->glyphs = glyphs;
    b->bits = bits;
    b->px = px;
    b->bold = bold;
    b->live = 1;
    b->face.glyphs = glyphs;
    b->face.bits = bits;
    b->face.count = BUILT_COUNT;
    b->face.first = BUILT_FIRST;
    b->face.ascent = (ascent > 0) ? ascent : px;
    b->face.height = (height > 0) ? height : px + 2;
    b->face.hi = NULL;          /* one face, replicated on a scaled screen */
    return 1;
}

/** @brief The built face for @p px and @p bold, making it if need be. */
static const tiku_desk_font_t *
built_face(int px, int bold)
{
    const tiku_desk_ttf_t *src = (bold && file_bold != NULL) ? file_bold
                                                            : file_regular;
    int i, spare = -1;

    if (src == NULL) {
        return NULL;
    }
    for (i = 0; i < BUILT_MAX; i++) {
        if (built[i].live && built[i].px == px && built[i].bold == bold) {
            return &built[i].face;
        }
        if (!built[i].live && spare < 0) {
            spare = i;
        }
    }
    if (spare < 0) {
        /* Every slot taken: the sizes in play have changed, so start the
         * collection again rather than grow it without end. */
        for (i = 0; i < BUILT_MAX; i++) {
            built_free(&built[i]);
        }
        spare = 0;
    }
    if (!built_make(&built[spare], src, px, bold)) {
        return NULL;
    }
    return &built[spare].face;
}

/** @brief Whether the interface is drawn from files right now. */
static int
using_files(void)
{
    return file_regular != NULL;
}

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
    const tiku_desk_font_t *plain = NULL, *bold = NULL;

    if (using_files()) {
        plain = built_face(face_sizes[rung], 0);
        bold = built_face(face_sizes[rung], 1);
    }
    if (plain == NULL || bold == NULL) {
        plain = plain_faces[family][rung];
        bold = bold_faces[family][rung];
    }
    current_plain = *plain;
    current_bold = *bold;
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

const char *
tiku_desk_font_current_family(void)
{
    if (using_files()) {
        return file_family;
    }
    return family_names[current_family];
}

int
tiku_desk_font_set_files(const char *regular, const char *bold)
{
    tiku_desk_ttf_t *r, *b = NULL;
    int i;

    if (regular == NULL) {
        return 0;
    }
    r = tiku_desk_ttf_open(regular);
    if (r == NULL) {
        return 0;               /* what we cannot read, we do not adopt */
    }
    if (bold != NULL) {
        b = tiku_desk_ttf_open(bold);
    }
    for (i = 0; i < BUILT_MAX; i++) {
        built_free(&built[i]);
    }
    tiku_desk_ttf_close(file_regular);
    tiku_desk_ttf_close(file_bold);
    file_regular = r;
    file_bold = b;
    snprintf(file_family, sizeof file_family, "%s",
             (tiku_desk_ttf_family(r) != NULL) ? tiku_desk_ttf_family(r)
                                               : "");
    if (current_size == 0) {
        current_size = 12;
    }
    adopt(current_family, rung_of(current_size));
    if (!using_files()) {
        return 0;
    }
    return 1;
}

void
tiku_desk_font_use_baked(void)
{
    int i;

    for (i = 0; i < BUILT_MAX; i++) {
        built_free(&built[i]);
    }
    tiku_desk_ttf_close(file_regular);
    tiku_desk_ttf_close(file_bold);
    file_regular = NULL;
    file_bold = NULL;
    file_family[0] = '\0';
    if (current_size == 0) {
        current_size = 12;
    }
    adopt(current_family, rung_of(current_size));
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
    int rung;

    if (current_size == 0) {
        (void)tiku_desk_font_set_size(12);
    }
    rung = rung_of(px);
    if (using_files()) {
        const tiku_desk_font_t *face = built_face(face_sizes[rung], 0);

        if (face != NULL) {
            return face;
        }
    }
    return plain_faces[current_family][rung];
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
