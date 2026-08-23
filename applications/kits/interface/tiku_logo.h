/*
 * TikuOS kits -- the foundation TikuDesktop and TikuTracker
 * both stand on.
 *
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 *
 * tiku_logo.h - the TikuOS mark.
 *
 * Two wireframe cubes locked through one another, with the amber on the
 * edges where they meet.  Kept as the DRAWING rather than as a picture:
 * it is a dozen straight lines and one filled face, so it is described
 * here in the same 1024-unit space the artwork was drawn in and painted
 * through the outline rasteriser the faces already use.
 *
 * That is not neatness for its own sake.  A mark stored as pixels has
 * one size and is soft at every other, and this one has to sit in a
 * 16-pixel strip on the Deskbar and fill a panel in the About box; the
 * two are eight times apart.  Drawn, it is sharp at both, and it follows
 * the interface's own size the way everything else now does.
 *
 * SPDX-License-Identifier: Apache-2.0
 */
#ifndef TIKU_LOGO_H_
#define TIKU_LOGO_H_

#include "tiku_gfx.h"

/**
 * @brief Lay the artwork's own light ground down under the mark.
 *
 * For somewhere the mark is a TILE -- the About box -- where it should
 * look like the thing on the box.  Left off where the mark sits on a
 * surface of its own, such as the Deskbar's amber leaf, on which a pale
 * square would read as a sticker rather than a logo.
 */
#define TIKU_LOGO_GROUND 0x1u

/**
 * @brief Draw the mark, fitted to the largest square inside @p r.
 *
 * Centred in @p r, and never wider than it is tall: the artwork is
 * square and stretching a logo is worse than leaving room beside it.
 *
 * It is drawn three ways, because one drawing cannot survive the range
 * it is asked to cover -- a Deskbar strip and an About panel are eight
 * times apart:
 *
 *   32 px and up   both cubes, as drawn
 *   22 to 31       the near cube alone: two wireframes through each
 *                  other at that size are a tangle, not a picture
 *   under 22       the near cube FILLED, so it reads as a solid with a
 *                  lit face rather than as five grey lines
 *
 * On a palette without hue the whole thing goes to luminance, like every
 * other picture: a desktop asked for without colour does not get an
 * amber logo in the corner of it.
 */
void tiku_logo_paint(tiku_surface_t *s, tiku_rect_t r, unsigned flags);

/*---------------------------------------------------------------------------*/
/* Colour, at the time of drawing rather than at the time of building        */
/*---------------------------------------------------------------------------*/

/**
 * @brief One set of the seven colours the mark is made of.
 *
 * The mark is kept as geometry, so its colours are arguments to fills
 * that have not happened yet.  That is the whole reason it is worth
 * carrying the drawing around instead of a picture of it: a picture has
 * the colours baked into every pixel and can only be recoloured by going
 * back over them and guessing which were which.
 */
typedef struct {
    tiku_rgb_t ground;     /* the light square the mark sits on   */
    tiku_rgb_t dark;       /* the near cube's edges               */
    tiku_rgb_t grey;       /* the far cube's                      */
    tiku_rgb_t accent;     /* the lit edges, where they meet      */
    tiku_rgb_t tint;       /* the one filled face                 */
    tiku_rgb_t mix_a;      /* the two edges that are half lit     */
    tiku_rgb_t mix_b;
} tiku_logo_palette_t;

/** @brief How many sets are built in.  The first is the mark as drawn. */
int tiku_logo_palette_count(void);

/**
 * @brief Set @p i, carried @p toward_next of the way to set @p i + 1.
 *
 * The blend is of the COLOURS, not of two drawings, so a fade costs
 * exactly what a single frame costs -- there is no second rendering to
 * cross-dissolve with.  @p toward_next outside 0..1 is clamped, and the
 * set after the last is the first again.
 */
void tiku_logo_palette(int i, float toward_next, tiku_logo_palette_t *out);

/**
 * @brief Draw with the colours given, shaded by @p shade.
 *
 * @p shade follows tiku_tint: 1.0 draws the colours as they are, below
 * one lightens them, above one darkens.  It is what lets one mark sit
 * legibly on the Deskbar's amber leaf in the light theme and on the
 * deepened amber of the dark one, where the near cube's near-black edges
 * would otherwise disappear into the button.
 */
void tiku_logo_paint_with(tiku_surface_t *s, tiku_rect_t r, unsigned flags,
                          const tiku_logo_palette_t *p, float shade);

/**
 * @brief The mark's ground colour, for a caller that wants to match it.
 *
 * The artwork has a light ground of its own and keeps it in every theme
 * -- a logo whose colours follow the furniture is not a logo -- so
 * anything drawn hard against it should ask rather than assume.
 */
tiku_rgb_t tiku_logo_ground(void);

#endif /* TIKU_LOGO_H_ */
