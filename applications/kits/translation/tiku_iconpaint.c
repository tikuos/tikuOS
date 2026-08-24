/*
 * TikuOS kits -- the foundation TikuDesktop and TikuTracker
 * both stand on.
 *
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 *
 * tiku_iconpaint.c - compositing a rendered icon onto a surface.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include "tiku_dl.h"
#include "tiku_iconpaint.h"
#include "tiku_icons.h"

/** @brief Blend one premultiplied source pixel over an opaque destination. */
static tiku_rgb_t
over(uint32_t src, tiku_rgb_t dst)
{
    unsigned a = (src >> 24) & 0xFFu;
    unsigned inv = 255u - a;
    unsigned r, g, b;

    /* Source is already multiplied by its own alpha, so the destination just
     * loses the same fraction: dst * (1 - a) + src. */
    r = ((src >> 16) & 0xFFu) + (((dst >> 16) & 0xFFu) * inv + 127u) / 255u;
    g = ((src >> 8) & 0xFFu)  + (((dst >> 8) & 0xFFu) * inv + 127u) / 255u;
    b = (src & 0xFFu)         + ((dst & 0xFFu) * inv + 127u) / 255u;
    if (r > 255u) { r = 255u; }
    if (g > 255u) { g = 255u; }
    if (b > 255u) { b = 255u; }
    return TIKU_RGB(r, g, b);
}

/** @brief Composite an already-derived bitmap: the cached-look path. */
static int
paint_bmp(tiku_surface_t *s, const uint32_t *bmp, int x, int y,
          int size)
{
    int row, col;

    if (s == NULL || bmp == NULL || size <= 0) {
        return 0;
    }
    for (row = 0; row < size; row++) {
        int py = y + row;

        if (py < s->clip.y || py >= s->clip.y + s->clip.h ||
            py < 0 || py >= s->h) {
            continue;
        }
        for (col = 0; col < size; col++) {
            int px = x + col;
            uint32_t src = bmp[(size_t)row * (size_t)size + (size_t)col];

            if (px < s->clip.x || px >= s->clip.x + s->clip.w ||
                px < 0 || px >= s->w || ((src >> 24) & 0xFFu) == 0u) {
                continue;
            }
            tiku_pixel(s, px, py, over(src, tiku_peek(s, px, py)));
        }
    }
    return 1;
}

static int
paint(tiku_surface_t *s, const char *name, int x, int y, int size,
      float mix, tiku_rgb_t wash)
{
    const uint32_t *bmp;
    int row, col;
    unsigned m;
    /* Asked once for the whole icon, not per pixel: it cannot change
     * while one is being painted. */
    int mono = tiku_theme_achromatic();

    /*
     * The one thing a display list genuinely cannot carry.  Everything in
     * the Interface kit reduces to primitives that record; an icon is a
     * PICTURE, and the commands are drawing calls, not pixels.  So a
     * window with an icon in it is told it is no longer whole, and
     * whoever is about to put it on a wire sends the frame instead.
     */
    tiku_gfx_rec_miss(s);

    if (s == NULL || name == NULL || size <= 0) {
        return 0;
    }
    bmp = tiku_icons_bitmap(name, size);
    if (bmp == NULL) {
        return 0;
    }
    if (mix < 0.0f) { mix = 0.0f; }
    if (mix > 1.0f) { mix = 1.0f; }
    m = (unsigned)(mix * 255.0f + 0.5f);

    for (row = 0; row < size; row++) {
        int py = y + row;

        if (py < s->clip.y || py >= s->clip.y + s->clip.h ||
            py < 0 || py >= s->h) {
            continue;
        }
        for (col = 0; col < size; col++) {
            int px = x + col;
            uint32_t src = bmp[(size_t)row * (size_t)size + (size_t)col];
            tiku_rgb_t d;

            if (px < s->clip.x || px >= s->clip.x + s->clip.w ||
                px < 0 || px >= s->w || ((src >> 24) & 0xFFu) == 0u) {
                continue;
            }
            d = over(src, tiku_peek(s, px, py));
            if (m != 0u) {
                unsigned r = ((d >> 16) & 0xFFu) * (255u - m) +
                             ((wash >> 16) & 0xFFu) * m;
                unsigned g = ((d >> 8) & 0xFFu) * (255u - m) +
                             ((wash >> 8) & 0xFFu) * m;
                unsigned b = (d & 0xFFu) * (255u - m) + (wash & 0xFFu) * m;
                d = TIKU_RGB(r / 255u, g / 255u, b / 255u);
            }
            if (mono) {
                d = tiku_grey(d);
            }
            tiku_pixel(s, px, py, d);
        }
    }
    return 1;
}

int
tiku_icon_paint(tiku_surface_t *s, const char *name, int x, int y,
                    int size)
{
    return paint(s, name, x, y, size, 0.0f, 0);
}

int
tiku_icon_paint_dim(tiku_surface_t *s, const char *name, int x, int y,
                        int size, float mix, tiku_rgb_t wash)
{
    return paint(s, name, x, y, size, mix, wash);
}

int
tiku_icon_paint_selected(tiku_surface_t *s, const char *name, int x,
                             int y, int size)
{
    /*
     * Here rather than in paint(), because the washed-bitmap fast path
     * below does not go through paint() at all -- so on a cache HIT the
     * icon went onto the surface and the list said it was whole.  Which
     * made it a fault that only appeared the second time an icon was
     * drawn.
     */
    tiku_gfx_rec_miss(s);

    /* The source's exact transform (IV-018): every channel multiplied to
     * 66% brightness (168/255) with the alpha kept -- a darkened icon, not
     * a tinted one, so the art stays recognisable in any palette.  Derived
     * once and CACHED beside the plain render, so a selected composite
     * costs what a plain one does (IV-019); the wash falls back to the
     * per-pixel path only when the cache cannot hold.  A wash toward black
     * at 87/255 IS the 168/255 multiply. */
    {
        const uint32_t *w = tiku_icons_bitmap_washed(name, size,
            0x000000u, 87u);

        if (w != NULL) {
            return paint_bmp(s, w, x, y, size);
        }
    }
    return paint(s, name, x, y, size, 0.341f, TIKU_RGB(0, 0, 0));
}

void
tiku_thumb_paint(tiku_surface_t *s, const tiku_image_t *th,
                     int x, int y, int size)
{
    int py, px;

    if (s == NULL || th == NULL || th->px == NULL || size <= 0) {
        return;
    }
    for (py = 0; py < size; py++) {
        long sy = (long)py * th->h / size;

        for (px = 0; px < size; px++) {
            long sx = (long)px * th->w / size;
            uint32_t p = th->px[sy * th->w + sx];

            /* The transparent margin is left alone rather than painted:
             * that is what makes the thumbnail sit ON the row instead of
             * inside a square of its own (IV-034). */
            if ((p >> 24) != 0u) {
                tiku_pixel(s, x + px, y + py,
                                TIKU_RGB((p >> 16) & 0xffu,
                                              (p >> 8) & 0xffu, p & 0xffu));
            }
        }
    }
}

int
tiku_icon_hit(const char *name, int size, int px, int py)
{
    const uint32_t *bm;

    if (name == NULL || size <= 0 || px < 0 || py < 0 ||
        px >= size || py >= size) {
        return 0;
    }
    bm = tiku_icons_bitmap(name, size);
    if (bm == NULL) {
        /* No art: the whole square answers, which is better than an icon
         * that cannot be picked up at all. */
        return 1;
    }
    /* Any alpha at all counts.  A threshold would make the antialiased
     * rim undraggable, and the rim is most of what a small icon is. */
    return ((bm[(long)py * size + px] >> 24) != 0u);
}

void
tiku_paint_tint(tiku_surface_t *s, tiku_rect_t r,
                    tiku_rgb_t c, int a)
{
    tiku_gfx_rec_miss(s);   /* a wash over what is there has no op */

    int x, y;
    int cr = (int)((c >> 16) & 0xffu), cg = (int)((c >> 8) & 0xffu);
    int cb = (int)(c & 0xffu);

    if (s == NULL || s->px == NULL || a <= 0) {
        return;
    }
    for (y = r.y; y < r.y + r.h; y++) {
        if (y < s->clip.y || y >= s->clip.y + s->clip.h ||
            y < 0 || y >= s->h) {
            continue;
        }
        for (x = r.x; x < r.x + r.w; x++) {
            tiku_rgb_t px;
            int pr, pg, pb;

            if (x < s->clip.x || x >= s->clip.x + s->clip.w ||
                x < 0 || x >= s->w) {
                continue;
            }
            px = tiku_peek(s, x, y);
            pr = (int)((px >> 16) & 0xffu);
            pg = (int)((px >> 8) & 0xffu);
            pb = (int)(px & 0xffu);
            pr = (pr * (255 - a) + cr * a) / 255;
            pg = (pg * (255 - a) + cg * a) / 255;
            pb = (pb * (255 - a) + cb * a) / 255;
            tiku_pixel(s, x, y, TIKU_RGB(pr, pg, pb));
        }
    }
}

void
tiku_paint_band(tiku_surface_t *s, tiku_rect_t r,
                    int transparent)
{
    /*
     * No miss of its own: every road out of here is one -- the opaque
     * band inverts, and the transparent one is five tints -- and each of
     * those says so for itself.  Saying it again here would count one
     * band as six.
     */
    if (s == NULL || r.w <= 0 || r.h <= 0) {
        return;
    }
    if (!transparent) {
        tiku_invert_frame(s, r);
        return;
    }
    {
        /* The edge is tinted harder than the middle, so the band has a
         * definite boundary without hiding the row under it -- four
         * strips, because a fill of the whole rect would put the edge
         * colour under the interior too. */
        tiku_rect_t in = { r.x + 1, r.y + 1, r.w - 2, r.h - 2 };
        tiku_rect_t e;

        e = (tiku_rect_t){ r.x, r.y, r.w, 1 };
        tiku_paint_tint(s, e, TIKU_C_FOCUS, 128);
        e = (tiku_rect_t){ r.x, r.y + r.h - 1, r.w, 1 };
        tiku_paint_tint(s, e, TIKU_C_FOCUS, 128);
        e = (tiku_rect_t){ r.x, r.y, 1, r.h };
        tiku_paint_tint(s, e, TIKU_C_FOCUS, 128);
        e = (tiku_rect_t){ r.x + r.w - 1, r.y, 1, r.h };
        tiku_paint_tint(s, e, TIKU_C_FOCUS, 128);
        if (in.w > 0 && in.h > 0) {
            tiku_paint_tint(s, in, TIKU_C_SELECT, 90);
        }
    }
}

void
tiku_paint_image(tiku_surface_t *s, const tiku_image_t *im,
                     tiku_rect_t dst, tiku_rect_t clip)
{
    int px, py;

    if (s == NULL || im == NULL || im->px == NULL || im->w <= 0 ||
        im->h <= 0 || dst.w <= 0 || dst.h <= 0) {
        return;
    }
    if (s != NULL && s->record != NULL && s->record_depth == 0) {
        tiku_dl_miss(s->record);        /* a thumbnail is a picture too */
    }
    for (py = 0; py < dst.h; py++) {
        int y = dst.y + py;
        long sy = (long)py * im->h / dst.h;

        if (y < clip.y || y >= clip.y + clip.h) {
            continue;
        }
        for (px = 0; px < dst.w; px++) {
            int x = dst.x + px;
            long sx = (long)px * im->w / dst.w;
            uint32_t p;

            if (x < clip.x || x >= clip.x + clip.w) {
                continue;
            }
            p = im->px[sy * im->w + sx];
            /* Transparent pixels leave the ground alone, so a picture with
             * a hole in it shows what is behind rather than black. */
            if ((p >> 24) != 0u) {
                tiku_rgb_t c = TIKU_RGB((p >> 16) & 0xffu,
                                             (p >> 8) & 0xffu, p & 0xffu);

                /* A thumbnail is a picture too: on a desktop asked for
                 * without colour it is the loudest thing on the screen
                 * if it keeps its own. */
                tiku_pixel(s, x, y,
                                tiku_theme_achromatic() ? tiku_grey(c) : c);
            }
        }
    }
}

void
tiku_paint_text_halo(tiku_surface_t *s, const tiku_font_t *f,
                         int x, int y, const char *text, tiku_rgb_t ink)
{
    static const int dx[] = { -1, 0, 1, -1, 1, -1, 0, 1 };
    static const int dy[] = { -1, -1, -1, 0, 0, 1, 1, 1 };
    int luminance = ((int)((ink >> 16) & 0xffu) * 299 +
                     (int)((ink >> 8) & 0xffu) * 587 +
                     (int)(ink & 0xffu) * 114) / 1000;
    /* Light ink is haloed in black and dark ink in white, as the source
     * chooses shine for dark text and black for light. */
    tiku_rgb_t halo = (luminance > 127) ? TIKU_RGB(0, 0, 0)
                                             : TIKU_RGB(255, 255, 255);
    int i;

    if (s == NULL || f == NULL || text == NULL) {
        return;
    }
    for (i = 0; i < 8; i++) {
        tiku_text(s, f, x + dx[i], y + dy[i], text, halo);
    }
    tiku_text(s, f, x, y, text, ink);
}
