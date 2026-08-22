/*
 * The new Tracker for TikuOS.
 *
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 *
 * tiku_trk_hvif.h - the Haiku Vector Icon Format, parsed and rasterised.
 *
 * HVIF is the compact vector container Haiku stores in the BEOS:ICON
 * attribute and in #'VICN' resources: a magic word, then a style table, a
 * path table and a shape table, all counted by a single byte.  Geometry
 * lives in a nominal 64x64 design square, so an icon is rasterised straight
 * at the size it will be drawn rather than scaled from a bitmap.
 *
 * Format reimplemented from Haiku's src/libs/icon/flat_icon/ (Haiku,
 * haiku-os.org, MIT licensed).  The artwork itself carries the Open Tracker
 * License (Be Incorporated, 1991-2000); see tiku_trk_icons.h.
 *
 * SPDX-License-Identifier: Apache-2.0
 */
#ifndef TIKU_TRK_HVIF_H_
#define TIKU_TRK_HVIF_H_

#include <stddef.h>
#include <stdint.h>

#include "tiku_desk_gfx.h"

/** @brief A parsed icon.  The wire structures stay private to the .c. */
typedef struct tiku_trk_hvif tiku_trk_hvif_t;

/** @brief Enough room for any message tiku_trk_hvif_parse() writes. */
#define TIKU_TRK_HVIF_ERRLEN 96

/**
 * @brief Parse an HVIF blob into a renderable icon.
 *
 * The parse is strict in the one way the format allows a cheap end-to-end
 * check: it must consume @p data to its exact final byte.  Anything left
 * over means a desynchronisation upstream, so the blob is rejected rather
 * than half-drawn.
 *
 * @param data   HVIF bytes, starting with the "ncif" magic.
 * @param len    Length of @p data.
 * @param err    Filled with the reason on failure; may be NULL.
 * @param errlen Size of @p err.
 * @return A new icon, or NULL if the blob is malformed.
 */
tiku_trk_hvif_t *tiku_trk_hvif_parse(const void *data, size_t len,
                                     char *err, size_t errlen);

/** @brief Release an icon from tiku_trk_hvif_parse(). */
void tiku_trk_hvif_free(tiku_trk_hvif_t *icon);

/**
 * @brief Rasterise @p icon into a premultiplied ARGB bitmap.
 *
 * @p bmp receives @p w * @p h pixels of 0xAARRGGBB with the colour channels
 * already multiplied by alpha -- the layout tiku_desk_rgb_t uses, plus a
 * real alpha byte -- cleared to fully transparent first.  The design square
 * is fitted uniformly into the smaller of @p w and @p h and centred, so a
 * non-square request letterboxes instead of stretching.
 *
 * @param icon Parsed icon.
 * @param bmp  Caller-supplied buffer of at least @p w * @p h uint32_t.
 * @param w    Bitmap width in pixels, > 0.
 * @param h    Bitmap height in pixels, > 0.
 * @return 0 on success, -1 on a bad argument or an allocation failure.
 */
int tiku_trk_hvif_render(const tiku_trk_hvif_t *icon, uint32_t *bmp,
                         int w, int h);

/**
 * @brief Source-over a premultiplied bitmap onto an opaque desk surface.
 *
 * The surface has no alpha channel of its own, so the destination is taken
 * as fully opaque and only its colour is blended.  Respects the clip.
 *
 * @param s   Destination surface.
 * @param dx  Left edge of the blit, in surface coordinates.
 * @param dy  Top edge of the blit.
 * @param bmp Premultiplied 0xAARRGGBB pixels from tiku_trk_hvif_render().
 * @param w   Bitmap width.
 * @param h   Bitmap height.
 */
void tiku_trk_hvif_blit(tiku_desk_surface_t *s, int dx, int dy,
                        const uint32_t *bmp, int w, int h);

/** @brief Number of styles the icon carries.  0 if @p icon is NULL. */
int tiku_trk_hvif_style_count(const tiku_trk_hvif_t *icon);

/** @brief Number of paths the icon carries.  0 if @p icon is NULL. */
int tiku_trk_hvif_path_count(const tiku_trk_hvif_t *icon);

/** @brief Number of shapes the icon carries.  0 if @p icon is NULL. */
int tiku_trk_hvif_shape_count(const tiku_trk_hvif_t *icon);

#endif /* TIKU_TRK_HVIF_H_ */
