/*
 * Tiku Desktop -- graphical interface to TikuOS devices.
 *
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 *
 * tiku_desk_gfx.h - the drawing surface and the R5 colour vocabulary.
 *
 * A surface is a plain 32-bit buffer with a clip rectangle; every widget draws
 * through these calls, so the same code serves an X11 window, a PNG dump and
 * later a wasm canvas.
 *
 * SPDX-License-Identifier: Apache-2.0
 */
#ifndef TIKU_DESK_GFX_H_
#define TIKU_DESK_GFX_H_

#include <stddef.h>
#include <stdint.h>

/** @brief Packed 0x00RRGGBB; the alpha byte is unused. */
typedef uint32_t tiku_desk_rgb_t;

#define TIKU_DESK_RGB(r, g, b) \
    (((tiku_desk_rgb_t)(r) << 16) | ((tiku_desk_rgb_t)(g) << 8) | \
     (tiku_desk_rgb_t)(b))

/* The R5 palette (R5-SPEC.md).  Every shade of the panel comes from
 * tiku_desk_tint() rather than a second constant. */
#define TIKU_DESK_C_PANEL      TIKU_DESK_RGB(216, 216, 216)
#define TIKU_DESK_C_DOC        TIKU_DESK_RGB(255, 255, 255)
#define TIKU_DESK_C_TEXT       TIKU_DESK_RGB(0, 0, 0)
#define TIKU_DESK_C_TAB        TIKU_DESK_RGB(255, 203, 0)
#define TIKU_DESK_C_TAB_IDLE   TIKU_DESK_RGB(232, 232, 232)
#define TIKU_DESK_C_SELECT     TIKU_DESK_RGB(51, 102, 152)
#define TIKU_DESK_C_SELTEXT    TIKU_DESK_RGB(255, 255, 255)
#define TIKU_DESK_C_FOCUS      TIKU_DESK_RGB(0, 0, 229)
#define TIKU_DESK_C_BACKDROP   TIKU_DESK_RGB(51, 102, 152)

/* BeOS tint constants, so the code reads like the documentation. */
#define TIKU_DESK_LIGHTEN_MAX  0.00f
#define TIKU_DESK_LIGHTEN_2    0.70f
#define TIKU_DESK_LIGHTEN_1    0.90f
#define TIKU_DESK_NO_TINT      1.00f
#define TIKU_DESK_DARKEN_1     1.02f
#define TIKU_DESK_DARKEN_2     1.06f
#define TIKU_DESK_DARKEN_3     1.07f
#define TIKU_DESK_DARKEN_4     1.08f
#define TIKU_DESK_DARKEN_MAX   2.00f

typedef struct {
    int x, y, w, h;
} tiku_desk_rect_t;

typedef struct {
    tiku_desk_rgb_t  *px;        /* w*h pixels, row-major   */
    int               w, h;
    tiku_desk_rect_t  clip;
} tiku_desk_surface_t;

/** @brief Allocate a surface and fill it with @p bg.  NULL on failure. */
tiku_desk_surface_t *tiku_desk_surface_new(int w, int h, tiku_desk_rgb_t bg);

/** @brief Replace a surface with a newly cleared framebuffer. */
int tiku_desk_surface_resize(tiku_desk_surface_t *s, int w, int h,
                             tiku_desk_rgb_t bg);

void tiku_desk_surface_free(tiku_desk_surface_t *s);

/** @brief Restrict drawing to @p r (intersected with the surface). */
void tiku_desk_clip_set(tiku_desk_surface_t *s, tiku_desk_rect_t r);

/** @brief Drop the clip back to the whole surface. */
void tiku_desk_clip_reset(tiku_desk_surface_t *s);

/** @brief Apply a BeOS tint to a colour (see the constants above). */
tiku_desk_rgb_t tiku_desk_tint(tiku_desk_rgb_t c, float tint);

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
void tiku_desk_copy_bits(tiku_desk_surface_t *s, tiku_desk_rect_t src,
                         int dx, int dy);

/**
 * @brief Put one surface's pixels into another at @p x, @p y.
 *
 * How a window with pixels of its own reaches the screen: it keeps its
 * content between frames and hands the whole thing over each time, so a
 * frame that painted one row still shows the others.
 */
void tiku_desk_blit(tiku_desk_surface_t *dst, int x, int y,
                    const tiku_desk_surface_t *src);

void tiku_desk_fill(tiku_desk_surface_t *s, tiku_desk_rect_t r,
                    tiku_desk_rgb_t c);

/** @brief One-pixel outline just inside @p r. */
void tiku_desk_frame(tiku_desk_surface_t *s, tiku_desk_rect_t r,
                     tiku_desk_rgb_t c);

void tiku_desk_hline(tiku_desk_surface_t *s, int x, int y, int len,
                     tiku_desk_rgb_t c);
void tiku_desk_vline(tiku_desk_surface_t *s, int x, int y, int len,
                     tiku_desk_rgb_t c);
void tiku_desk_pixel(tiku_desk_surface_t *s, int x, int y, tiku_desk_rgb_t c);

/** @brief Expand a logical surface into a native framebuffer. */
void tiku_desk_scale_pixels(tiku_desk_rgb_t *destination,
                            int destination_width, int destination_height,
                            const tiku_desk_rgb_t *source,
                            int source_width, int source_height);

/**
 * @brief Invert the pixels along a rectangle's outline.
 *
 * Inversion, not a colour: it is visible over whatever it lands on and it
 * undoes itself when stroked again, which is what lets an animation or a
 * rubber band be drawn and erased without saving what was underneath.
 */
void tiku_desk_invert_frame(tiku_desk_surface_t *s, tiku_desk_rect_t r);

/**
 * @brief A one-pixel bevel: @p light on top and left, @p shadow elsewhere.
 *
 * The whole R5 look is this call applied in pairs; nothing is gradient-shaded.
 */
void tiku_desk_bevel(tiku_desk_surface_t *s, tiku_desk_rect_t r,
                     tiku_desk_rgb_t light, tiku_desk_rgb_t shadow);

/** @brief Shrink a rectangle by @p n on every side. */
tiku_desk_rect_t tiku_desk_inset(tiku_desk_rect_t r, int n);

/** @brief Write the surface as a PNG; 0 on success. */
int tiku_desk_surface_png(const tiku_desk_surface_t *s, const char *path);

#endif /* TIKU_DESK_GFX_H_ */
