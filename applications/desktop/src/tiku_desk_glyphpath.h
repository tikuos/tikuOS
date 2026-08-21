/*
 * Tiku Desktop -- graphical interface to TikuOS devices.
 *
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 *
 * tiku_desk_glyphpath.h - one outline, however it was described.
 *
 * TrueType describes a glyph in quadratics and CFF describes it in
 * cubics, but a rasteriser wants neither: it wants edges.  Both readers
 * push their curves in here, in DEVICE space -- pixels, y downwards from
 * the baseline -- and this flattens them by how big they actually are on
 * the screen, so a curve is smooth at forty pixels without being ten
 * segments of nothing at ten.
 *
 * The coverage itself is signed area accumulated per cell and resolved
 * with a prefix sum, which is what the icon renderer resolves its paths
 * with: coverage that is computed rather than sampled has no jaggies to
 * average away, and that matters most at the sizes an interface uses.
 *
 * SPDX-License-Identifier: Apache-2.0
 */
#ifndef TIKU_DESK_GLYPHPATH_H_
#define TIKU_DESK_GLYPHPATH_H_

typedef struct tiku_desk_path tiku_desk_path_t;

/**
 * @brief How font units become device pixels, vertically grid-fitted.
 *
 * X is scaled and left alone, so advances and the spacing between
 * letters stay as smooth as the designer drew them.  Y is scaled and
 * then nudged, so that the lines a reader's eye follows -- the baseline,
 * the top of the lowercase, the top of the capitals -- land ON pixel
 * boundaries instead of straddling two rows at half strength.  That is
 * what "hinted" buys at 10 and 12 px, and almost all of what it buys.
 *
 * With @c zones at 0 this is a plain scale, which is what a face we did
 * not measure gets.
 */
#define TIKU_DESK_HINT_ZONES 3

typedef struct {
    float scale;
    int   zones;
    float from[TIKU_DESK_HINT_ZONES];   /* font units, ascending */
    float shift[TIKU_DESK_HINT_ZONES];  /* device pixels to nudge by */
} tiku_desk_hint_t;

/** @brief The device y for font-unit @p y, sign flipped for the screen. */
float tiku_desk_hint_y(const tiku_desk_hint_t *hint, float y);

/** @brief The device x for font-unit @p x. */
float tiku_desk_hint_x(const tiku_desk_hint_t *hint, float x);

tiku_desk_path_t *tiku_desk_path_new(void);
void tiku_desk_path_free(tiku_desk_path_t *path);

/** @brief Forget the outline, keeping the room it was built in. */
void tiku_desk_path_reset(tiku_desk_path_t *path);

/**
 * @brief Whether anything has gone wrong since the last reset.
 *
 * Every builder call is allowed to fail silently -- a reader deep in a
 * charstring has nowhere useful to report to -- so failure is asked
 * about once, at the end.
 */
int tiku_desk_path_failed(const tiku_desk_path_t *path);

void tiku_desk_path_move(tiku_desk_path_t *path, float x, float y);
void tiku_desk_path_line(tiku_desk_path_t *path, float x, float y);
void tiku_desk_path_quad(tiku_desk_path_t *path, float cx, float cy,
                         float x, float y);
void tiku_desk_path_cubic(tiku_desk_path_t *path, float ax, float ay,
                          float bx, float by, float x, float y);

/** @brief Shut the contour back to where the last move put the pen. */
void tiku_desk_path_close(tiku_desk_path_t *path);

/**
 * @brief The whole outline's extent, in whole pixels, with a margin.
 *
 * The margin is the accumulator's: an edge writes into the cell past the
 * one it covers, and that cell has to be inside the map for the prefix
 * sum to resolve the last column of ink.
 */
void tiku_desk_path_bounds(const tiku_desk_path_t *path, int *x0, int *y0,
                           int *w, int *h);

/**
 * @brief Coverage for the box @p x0,@p y0,@p w,@p h.
 *
 * @return w*h bytes the caller frees, or NULL.
 */
unsigned char *tiku_desk_path_render(const tiku_desk_path_t *path, int x0,
                                     int y0, int w, int h);

#endif /* TIKU_DESK_GLYPHPATH_H_ */
