/*
 * TikuOS kits -- the foundation TikuDesktop and TikuTracker
 * both stand on.
 *
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 *
 * tiku_plaque.h - the plaque a program puts up while it gathers itself.
 *
 * The big programs of the old systems came up behind one of these: the
 * artwork across the top, the name below it, a line saying what is
 * being gathered, the people it is by, and the version in the corner.
 * One painter for all of that, here, so every program's announcement is
 * the same announcement wearing different words -- and so the artwork
 * is the system's own mark in the system's own colours, not a picture
 * with its colours baked in.
 *
 * SPDX-License-Identifier: Apache-2.0
 */
#ifndef TIKU_PLAQUE_H_
#define TIKU_PLAQUE_H_

#include "tiku_gfx.h"

/** @brief How many small lines a plaque will carry. */
#define TIKU_PLAQUE_LINES 6

/**
 * @brief Paint a plaque over all of @p s.
 *
 * @p name is the big words.  @p lines are the small ones -- who it is
 * by, what it stands on -- painted as a block the way the old plaques
 * carried their credits.  @p version sits in the bottom-left corner and
 * the mark sits small in the bottom-right, both there whether or not
 * anything is still being gathered.
 *
 * @p status, when given, is what is being gathered RIGHT NOW, painted
 * under the name where those plaques always said it; NULL paints the
 * finished plaque, which is what an About box is.
 */
void tiku_plaque_paint(tiku_surface_t *s, const char *name,
                            const char *const *lines, int nlines,
                            const char *version, const char *status);

#endif /* TIKU_PLAQUE_H_ */
