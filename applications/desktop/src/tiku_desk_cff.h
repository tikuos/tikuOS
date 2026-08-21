/*
 * Tiku Desktop -- graphical interface to TikuOS devices.
 *
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 *
 * tiku_desk_cff.h - the other kind of outline.
 *
 * An OpenType file with PostScript outlines keeps them in a CFF table:
 * a stack of INDEXes and DICTs, and per glyph a Type 2 charstring, which
 * is a little program that draws.  Sixty of the three hundred faces on a
 * plain Fedora are these, so refusing them refuses a fifth of what a
 * person could drop in the fonts folder.
 *
 * Cubics, where TrueType has quadratics; both end up in the same path.
 *
 * SPDX-License-Identifier: Apache-2.0
 */
#ifndef TIKU_DESK_CFF_H_
#define TIKU_DESK_CFF_H_

#include <stddef.h>

#include "tiku_desk_glyphpath.h"

typedef struct tiku_desk_cff tiku_desk_cff_t;

/**
 * @brief Read the CFF table at @p data, @p len bytes of it.
 *
 * The bytes are borrowed, not copied: they must outlive the handle.
 *
 * @return NULL for a table we cannot draw from.
 */
tiku_desk_cff_t *tiku_desk_cff_open(const unsigned char *data, size_t len);
void tiku_desk_cff_close(tiku_desk_cff_t *cff);

/** @brief How many glyphs the charstrings index holds. */
int tiku_desk_cff_count(const tiku_desk_cff_t *cff);

/**
 * @brief Run glyph @p gid's charstring into @p path.
 *
 * Coordinates are pushed in device space: multiplied by @p scale, with y
 * downwards, which is what the path and everything below it expect.
 *
 * @return 1 when the glyph was drawn (an empty glyph counts).
 */
int tiku_desk_cff_outline(const tiku_desk_cff_t *cff, unsigned gid,
                          const tiku_desk_hint_t *hint,
                          tiku_desk_path_t *path);

/** @brief Units per em as the CFF's own FontMatrix states it. */
float tiku_desk_cff_upem(const tiku_desk_cff_t *cff);

#endif /* TIKU_DESK_CFF_H_ */
