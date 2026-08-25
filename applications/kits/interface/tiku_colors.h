/*
 * TikuOS kits -- the foundation TikuDesktop and TikuTracker
 * both stand on.
 *
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 *
 * tiku_colors.h - a grid of colours, one of them chosen.
 *
 * The R5 colour control is a palette a colour is PICKED from, not mixed
 * in: a person choosing a backdrop wants to point at a colour, and the
 * three sliders that would let them say #4a7fb2 are a different tool for
 * a different day.  So this holds a fixed grid, which one is current,
 * where each swatch sits and which one a click lands on.
 *
 * The desktop can already be handed a colour by dropping one on it; what
 * it could not do is offer the choice.
 *
 * SPDX-License-Identifier: Apache-2.0
 */
#ifndef TIKU_COLORS_H_
#define TIKU_COLORS_H_

#include "tiku_gfx.h"

#define TIKU_COLORS_MAX 64

typedef struct {
    tiku_rgb_t swatch[TIKU_COLORS_MAX];
    int        count;
    int        cols;       /* swatches across; rows follow from count */
    int        current;    /* -1 when nothing is chosen               */
} tiku_colors_t;

/** @brief An empty palette @p cols swatches wide. */
void tiku_colors_init(tiku_colors_t *c, int cols);

/**
 * @brief Fill with a spread of hues plus a grey ramp, as a default.
 *
 * What a control offers when its caller has no palette of its own: a
 * readable set rather than an empty grid.
 */
void tiku_colors_default(tiku_colors_t *c);

/** @brief Add one swatch.  @return its index, or -1 when full. */
int tiku_colors_add(tiku_colors_t *c, tiku_rgb_t rgb);

/** @brief How many swatches there are. */
int tiku_colors_count(const tiku_colors_t *c);

/** @brief The chosen index, or -1. */
int tiku_colors_current(const tiku_colors_t *c);

/** @brief The chosen colour; black when nothing is chosen. */
tiku_rgb_t tiku_colors_value(const tiku_colors_t *c);

/** @brief Choose @p index, clamped.  @return the chosen index. */
int tiku_colors_select(tiku_colors_t *c, int index);

/**
 * @brief Choose the swatch nearest @p rgb.
 *
 * So a control opened on a colour the palette holds no exact match for
 * still opens somewhere sensible instead of unset.  @return the index.
 */
int tiku_colors_select_nearest(tiku_colors_t *c, tiku_rgb_t rgb);

/** @brief Where swatch @p index sits in the grid drawn in @p r. */
tiku_rect_t tiku_colors_rect(const tiku_colors_t *c, tiku_rect_t r,
                             int index);

/** @brief Which swatch a point lands on, or -1. */
int tiku_colors_hit(const tiku_colors_t *c, tiku_rect_t r, int x, int y);

/** @brief Draw the grid, the chosen swatch ringed. */
void tiku_colors_draw(const tiku_colors_t *c, tiku_surface_t *s,
                      tiku_rect_t r);

#endif /* TIKU_COLORS_H_ */
