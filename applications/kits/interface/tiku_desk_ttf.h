/*
 * Tiku Desktop -- graphical interface to TikuOS devices.
 *
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 *
 * tiku_desk_ttf.h - reading a font somebody dropped in.
 *
 * A TrueType file is a table directory, a character map and a set of
 * quadratic outlines; rasterising one is the same signed-area coverage
 * the icon renderer resolves its paths with.  No font library: the point
 * of the fonts directory is that a device can be handed a face over a
 * wire and draw with it, which rules out asking the host for help.
 *
 * SPDX-License-Identifier: Apache-2.0
 */
#ifndef TIKU_DESK_TTF_H_
#define TIKU_DESK_TTF_H_

typedef struct tiku_desk_ttf tiku_desk_ttf_t;

/**
 * @brief Open a TrueType file.
 *
 * @return NULL for anything we cannot draw with -- a PostScript-outline
 *         .otf included, which says so in its header rather than lying.
 */
tiku_desk_ttf_t *tiku_desk_ttf_open(const char *path);
void tiku_desk_ttf_close(tiku_desk_ttf_t *ttf);

/** @brief The family name the file gives itself. */
const char *tiku_desk_ttf_family(const tiku_desk_ttf_t *ttf);

/** @brief Whether the face calls itself bold. */
int tiku_desk_ttf_bold(const tiku_desk_ttf_t *ttf);

/** @brief Whether @p path looks like a face we could open, cheaply. */
int tiku_desk_ttf_is_font(const char *path);

/**
 * @brief Ascent and line height in whole pixels at @p px.
 *
 * Both may be NULL.
 */
void tiku_desk_ttf_metrics(const tiku_desk_ttf_t *ttf, int px,
                           int *ascent, int *height);

/** @brief One rasterised glyph.  @c cover is w*h bytes of coverage. */
typedef struct {
    int            adv;
    int            w, h;
    int            ox, oy;      /* from the pen, and from the baseline */
    unsigned char *cover;
} tiku_desk_ttf_glyph_t;

/**
 * @brief Draw @p cp at @p px pixels.
 *
 * A code point the file has no glyph for is not drawn: the caller shows
 * what it shows for anything else it hasn't got.
 *
 * @return 1 when @p out was filled, 0 otherwise.
 */
int tiku_desk_ttf_render(const tiku_desk_ttf_t *ttf, unsigned cp, int px,
                         tiku_desk_ttf_glyph_t *out);

/** @brief Whether the file has a glyph for @p cp at all. */
int tiku_desk_ttf_has(const tiku_desk_ttf_t *ttf, unsigned cp);

void tiku_desk_ttf_free_glyph(tiku_desk_ttf_glyph_t *glyph);

#endif /* TIKU_DESK_TTF_H_ */
