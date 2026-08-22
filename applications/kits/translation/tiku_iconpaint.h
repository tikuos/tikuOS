/*
 * TikuOS kits -- the foundation TikuDesktop and TikuTracker
 * both stand on.
 *
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 *
 * tiku_iconpaint.h - putting a rendered icon on a surface.
 *
 * The icon cache hands back premultiplied ARGB; the surface is opaque RGB.
 * One compositor does that conversion for everyone who draws an icon, so the
 * blend rule exists once rather than once per call site.
 *
 * SPDX-License-Identifier: Apache-2.0
 */
#ifndef TIKU_ICONPAINT_H_
#define TIKU_ICONPAINT_H_

#include "tiku_font.h"
#include "tiku_gfx.h"
#include "tiku_thumb.h"

/**
 * @brief Composite icon @p name at @p size over @p s with its top-left at
 *        (@p x, @p y).
 *
 * Rendering is cached per name and size, so drawing the same icon in every
 * row of a list costs one rasterisation, not one per row.
 *
 * @return 1 when it was drawn, 0 for an unknown name or a failed render --
 *         the caller can then fall back rather than leaving a hole.
 */
int tiku_icon_paint(tiku_surface_t *s, const char *name,
                        int x, int y, int size);

/**
 * @brief Composite it dimmed, for a disabled or minimised item.
 *
 * @param mix 0.0 leaves the icon untouched, 1.0 dissolves it into @p over.
 */
int tiku_icon_paint_dim(tiku_surface_t *s, const char *name,
                            int x, int y, int size, float mix,
                            tiku_rgb_t over);

/**
 * @brief Composite the SELECTED variant of an icon (PVL-082).
 *
 * Selection has to read on the icon and not only on the label, or a row
 * whose label is off the right edge shows no selection at all.  Haiku keeps
 * a second bitmap per icon for this; one wash toward the selection colour
 * gives the same two visual states without a second cache.
 */
int tiku_icon_paint_selected(tiku_surface_t *s, const char *name,
                                 int x, int y, int size);

/**
 * @brief Draw a loaded thumbnail into @p s at @p size, alpha-blended.
 *
 * Scaled down at draw time rather than stored per size: one 128 square
 * answers every icon size the view can ask for.
 */
void tiku_thumb_paint(tiku_surface_t *s,
                          const tiku_image_t *th, int x, int y,
                          int size);

/**
 * @brief Whether the pixel at @p px, @p py of icon @p name is opaque.
 *
 * A drag starts only from a pixel that is actually part of the picture: an
 * icon is not a rectangle, and arming a drag from its transparent corner
 * would make the gap between two icons draggable (IV-057).
 */
int tiku_icon_hit(const char *name, int size, int px, int py);

/**
 * @brief Blend @p c over the pixels of @p r at @p a / 255.
 *
 * The floor draws opaque colours; the tinted rubber band is the one place
 * a rectangle has to be visible AND let what it covers show through -- a
 * solid fill over the icons hides exactly what is being selected.  It
 * lives here because this is the file that owns alpha (PVL-069, PVS-030).
 */
void tiku_paint_tint(tiku_surface_t *s, tiku_rect_t r,
                         tiku_rgb_t c, int a);

/**
 * @brief Draw a rubber band the way @p transparent asks for.
 *
 * Tinted (Tracker's default) puts an edge and a lighter interior over what
 * it covers; otherwise the outline is INVERTED, which is visible over
 * whatever it lands on and undoes itself (PVL-069, PVS-030, TS-049).
 */
void tiku_paint_band(tiku_surface_t *s, tiku_rect_t r,
                         int transparent);

/**
 * @brief Draw a picture into @p dst, scaled to it, clipped to @p clip.
 *
 * The one blit a background is made of: the placement decides where each
 * copy goes and this puts it there, so tiled, centred and scaled are the
 * same drawing done to different rectangles (CW-020, AW-016).
 */
void tiku_paint_image(tiku_surface_t *s, const tiku_image_t *im,
                          tiku_rect_t dst, tiku_rect_t clip);

/**
 * @brief Draw @p text with a contrast halo around it (PVL-071, IV-049).
 *
 * Over a picture, a label in one colour is unreadable wherever the picture
 * is the same colour.  Tracker draws the string several more times in a
 * contrasting shade before the real one; this floor has no alpha in its
 * text, so the halo is the same string at the eight neighbouring pixels,
 * which is the readable part of that effect.
 */
void tiku_paint_text_halo(tiku_surface_t *s,
                              const tiku_font_t *f, int x, int y,
                              const char *text, tiku_rgb_t ink);

#endif /* TIKU_ICONPAINT_H_ */
