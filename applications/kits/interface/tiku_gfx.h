/*
 * TikuOS kits -- the foundation TikuDesktop and TikuTracker
 * both stand on.
 *
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 *
 * tiku_gfx.h - the drawing surface and the R5 colour vocabulary.
 *
 * A surface is a plain 32-bit buffer with a clip rectangle; every widget draws
 * through these calls, so the same code serves an X11 window, a PNG dump and
 * later a wasm canvas.
 *
 * SPDX-License-Identifier: Apache-2.0
 */
#ifndef TIKU_GFX_H_
#define TIKU_GFX_H_

#include <stddef.h>
#include <stdint.h>

/** @brief Packed 0x00RRGGBB; the alpha byte is unused. */
typedef uint32_t tiku_rgb_t;

#define TIKU_RGB(r, g, b) \
    (((tiku_rgb_t)(r) << 16) | ((tiku_rgb_t)(g) << 8) | \
     (tiku_rgb_t)(b))

/* The R5 palette (R5-SPEC.md).  Every shade of the panel comes from
 * tiku_tint() rather than a second constant. */
/*
 * The theme: every SEMANTIC colour the toolkit and its applications paint
 * with, held in one runtime table (the Appearance Manager lesson: the
 * appearance may change, the layout may not).  The names below keep their
 * classic spellings but read through the live theme, so a swapped table
 * recolours every widget without a caller changing -- and the default
 * table IS the BeOS R5 look, which is what every pixel test pins.
 *
 * Only semantic colours live here.  Pictorial art (icon bodies, badge
 * glyphs) and derived shades (tints, bevels) stay where they are painted:
 * a theme names roles, not pixels.
 */
typedef struct {
    tiku_rgb_t panel;      /* control surfaces                     */
    tiku_rgb_t doc;        /* document/list ground                 */
    tiku_rgb_t text;       /* ink                                  */
    tiku_rgb_t tab;        /* the active window tab                */
    tiku_rgb_t tab_idle;
    tiku_rgb_t select;     /* selection ground                     */
    tiku_rgb_t seltext;    /* ink over a selection                 */
    tiku_rgb_t focus;      /* keyboard-navigation marks            */
    tiku_rgb_t backdrop;   /* the desktop behind everything        */
    tiku_rgb_t danger;     /* the stop severity                    */
    tiku_rgb_t note;       /* tooltip/expander paper               */
    /* Ink over the active tab, split from the body ink so a dark theme
     * can keep the yellow tab -- the Be identity -- and still label it
     * legibly. */
    tiku_rgb_t tab_text;
} tiku_theme_t;

/** @brief The live theme.  Never NULL. */
const tiku_theme_t *tiku_theme(void);

/** @brief Swap the theme; NULL restores the default (the R5 look). */
void tiku_theme_set(const tiku_theme_t *theme);

/** @brief The built-in dark table: the same desktop after dusk. */
const tiku_theme_t *tiku_theme_dark(void);

/** @brief The built-in table with no hue in it at all. */
const tiku_theme_t *tiku_theme_mono(void);

/**
 * @brief Whether the live table names no hue anywhere.
 *
 * A theme names ROLES, and pictorial art -- icon bodies, thumbnails --
 * is deliberately outside that: an icon is a picture, not a surface.
 * But a desktop asked for without colour and then handed a red flower
 * has not been given what it asked for, so the art asks this and drains
 * itself.  Derived from the table, so it cannot disagree with it.
 */
int tiku_theme_achromatic(void);

/** @brief @p c at its luminance: the same brightness, no hue. */
tiku_rgb_t tiku_grey(tiku_rgb_t c);

#define TIKU_C_PANEL      (tiku_theme()->panel)
#define TIKU_C_DOC        (tiku_theme()->doc)
#define TIKU_C_TEXT       (tiku_theme()->text)
#define TIKU_C_TAB        (tiku_theme()->tab)
#define TIKU_C_TAB_IDLE   (tiku_theme()->tab_idle)
#define TIKU_C_SELECT     (tiku_theme()->select)
#define TIKU_C_SELTEXT    (tiku_theme()->seltext)
#define TIKU_C_FOCUS      (tiku_theme()->focus)
#define TIKU_C_BACKDROP   (tiku_theme()->backdrop)
#define TIKU_C_DANGER     (tiku_theme()->danger)
#define TIKU_C_NOTE       (tiku_theme()->note)
#define TIKU_C_TABTEXT    (tiku_theme()->tab_text)

/* BeOS tint constants, so the code reads like the documentation. */
#define TIKU_LIGHTEN_MAX  0.00f
#define TIKU_LIGHTEN_2    0.70f
#define TIKU_LIGHTEN_1    0.90f
#define TIKU_NO_TINT      1.00f
#define TIKU_DARKEN_1     1.02f
#define TIKU_DARKEN_2     1.06f
#define TIKU_DARKEN_3     1.07f
#define TIKU_DARKEN_4     1.08f
#define TIKU_DARKEN_MAX   2.00f

typedef struct {
    int x, y, w, h;
} tiku_rect_t;

typedef struct {
    tiku_rgb_t  *px;        /* native pixels, row-major */
    int               w, h;      /* LOGICAL size             */
    tiku_rect_t  clip;      /* logical, like every rect */
    /* Native pixels per logical pixel (0 reads as 1).  Every call here
     * takes logical coordinates; a scaled surface simply keeps px at
     * (w*scale) x (h*scale) and paints scale x scale blocks -- except
     * text, which carries a real 2x face and gains true detail. */
    int               scale;
} tiku_surface_t;

/** @brief Allocate a surface and fill it with @p bg.  NULL on failure. */
tiku_surface_t *tiku_surface_new(int w, int h, tiku_rgb_t bg);

/** @brief Give a surface @p scale native pixels per logical one. */
int tiku_surface_rescale(tiku_surface_t *s, int scale,
                              tiku_rgb_t bg);

/** @brief The logical pixel at (x, y), or 0 outside the surface. */
tiku_rgb_t tiku_peek(const tiku_surface_t *s, int x, int y);

/** @brief Replace a surface with a newly cleared framebuffer. */
int tiku_surface_resize(tiku_surface_t *s, int w, int h,
                             tiku_rgb_t bg);

void tiku_surface_free(tiku_surface_t *s);

/** @brief Restrict drawing to @p r (intersected with the surface). */
void tiku_clip_set(tiku_surface_t *s, tiku_rect_t r);

/** @brief Drop the clip back to the whole surface. */
void tiku_clip_reset(tiku_surface_t *s);

/** @brief Apply a BeOS tint to a colour (see the constants above). */
tiku_rgb_t tiku_tint(tiku_rgb_t c, float tint);

/**
 * @brief Move a block of pixels within the surface (CopyBits).
 *
 * What a list does when a row is inserted or removed: the rows below it are
 * the same pixels a row further down or up.  Source and destination are both
 * clipped, and the two may overlap.
 *
 * @param src Where the pixels are now.
 * @param dx  How far right to put them, negative for left.
 * @param dy  How far down, negative for up.
 */
void tiku_copy_bits(tiku_surface_t *s, tiku_rect_t src,
                         int dx, int dy);

/**
 * @brief Put one surface's pixels into another at @p x, @p y.
 *
 * How a window with pixels of its own reaches the screen: it keeps its
 * content between frames and hands the whole thing over each time, so a
 * frame that painted one row still shows the others.
 */
void tiku_blit(tiku_surface_t *dst, int x, int y,
                    const tiku_surface_t *src);

void tiku_fill(tiku_surface_t *s, tiku_rect_t r,
                    tiku_rgb_t c);

/** @brief One-pixel outline just inside @p r. */
void tiku_frame(tiku_surface_t *s, tiku_rect_t r,
                     tiku_rgb_t c);

void tiku_hline(tiku_surface_t *s, int x, int y, int len,
                     tiku_rgb_t c);
void tiku_vline(tiku_surface_t *s, int x, int y, int len,
                     tiku_rgb_t c);
void tiku_pixel(tiku_surface_t *s, int x, int y, tiku_rgb_t c);

/** @brief Expand a logical surface into a native framebuffer. */
void tiku_scale_pixels(tiku_rgb_t *destination,
                            int destination_width, int destination_height,
                            const tiku_rgb_t *source,
                            int source_width, int source_height);

/**
 * @brief Invert the pixels along a rectangle's outline.
 *
 * Inversion, not a colour: it is visible over whatever it lands on and it
 * undoes itself when stroked again, which is what lets an animation or a
 * rubber band be drawn and erased without saving what was underneath.
 */
void tiku_invert_frame(tiku_surface_t *s, tiku_rect_t r);

/**
 * @brief A one-pixel bevel: @p light on top and left, @p shadow elsewhere.
 *
 * The whole R5 look is this call applied in pairs; nothing is gradient-shaded.
 */
void tiku_bevel(tiku_surface_t *s, tiku_rect_t r,
                     tiku_rgb_t light, tiku_rgb_t shadow);

/** @brief Shrink a rectangle by @p n on every side. */
tiku_rect_t tiku_inset(tiku_rect_t r, int n);

/** @brief Write the surface as a PNG; 0 on success. */
int tiku_surface_png(const tiku_surface_t *s, const char *path);

#endif /* TIKU_GFX_H_ */
